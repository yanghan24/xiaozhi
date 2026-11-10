"""Durable grant quarantine and idempotent delivery; contains no business Keys."""
import hashlib
import logging
import os
from pathlib import Path
import sqlite3
import threading
import time
from contextlib import contextmanager


class RejectionStoreUnavailable(Exception):
    pass


class RejectionOutbox:
    # All authorities in one process fail closed if their shared storage fails.
    _failed = set()
    _failure_lock = threading.Lock()
    MAX_PENDING = 1024
    MAX_RECORDS = 4096

    def __init__(self, authority_url, deliver, directory):
        directory = Path(directory)
        directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.path = str(directory.resolve() / (hashlib.sha256(authority_url.encode()).hexdigest() + '.sqlite3'))
        if any(str(candidate.resolve()) != self.path for candidate in directory.glob('*.sqlite3')):
            raise RejectionStoreUnavailable('Changing authority requires rejection-state migration')
        fd = os.open(self.path, os.O_CREAT | os.O_RDWR | getattr(os, 'O_NOFOLLOW', 0), 0o600)
        os.close(fd)
        os.chmod(self.path, 0o600)
        self.deliver = deliver
        self.wake = threading.Event()
        with self._connect() as db:
            db.execute('CREATE TABLE IF NOT EXISTS rejections (scope TEXT PRIMARY KEY, receipt TEXT NOT NULL, pending INTEGER NOT NULL, attempts INTEGER NOT NULL, next_attempt REAL NOT NULL, acknowledged_at REAL, subject TEXT NOT NULL, authorization_order INTEGER NOT NULL)')
            db.execute('CREATE TABLE IF NOT EXISTS health (id INTEGER PRIMARY KEY CHECK(id=1), fault INTEGER NOT NULL)')
            db.execute('INSERT OR IGNORE INTO health VALUES (1,0)')
            if db.execute('SELECT fault FROM health WHERE id=1').fetchone()[0]:
                raise RejectionStoreUnavailable('Product rejection storage requires recovery')
        threading.Thread(target=self._run, name='product-key-rejections', daemon=True).start()

    def _fail(self):
        with self._failure_lock:
            first_failure = self.path not in self._failed
            self._failed.add(self.path)
        if first_failure:
            logging.getLogger(__name__).error('Product rejection storage requires operator recovery')
        # Persist capacity/integrity failures so a restart cannot silently clear quarantine.
        # An unreadable database is itself fail-closed on the next startup.
        try:
            db = sqlite3.connect(self.path, timeout=2)
            try:
                with db:
                    db.execute('UPDATE health SET fault=1 WHERE id=1')
            finally:
                db.close()
        except Exception:
            pass
        raise RejectionStoreUnavailable('Product rejection storage unavailable') from None

    @contextmanager
    def _connect(self):
        db = None
        try:
            db = sqlite3.connect(self.path, timeout=2)
            db.execute('PRAGMA synchronous=FULL')
            with db:
                yield db
        except Exception:
            self._fail()
        finally:
            if db is not None:
                db.close()

    def blocked(self, scope, subject, permission_order):
        with self._failure_lock:
            if self.path in self._failed:
                raise RejectionStoreUnavailable('Product rejection storage unavailable')
        try:
            with self._connect() as db:
                if db.execute('SELECT fault FROM health WHERE id=1').fetchone()[0]:
                    raise RejectionStoreUnavailable('Product rejection storage requires recovery')
                return db.execute('SELECT 1 FROM rejections WHERE scope=? OR (subject=? AND authorization_order>=?) LIMIT 1', (scope, subject, permission_order)).fetchone() is not None
        except Exception:
            self._fail()

    def reject(self, scope, receipt, subject, authorization_order):
        try:
            with self._connect() as db:
                db.execute('BEGIN IMMEDIATE')
                # Keep acknowledged tombstones longer than any in-flight transport check.
                db.execute('DELETE FROM rejections WHERE pending=0 AND acknowledged_at<?', (time.time()-86400,))
                if db.execute('SELECT 1 FROM rejections WHERE scope=?', (scope,)).fetchone():
                    return
                if db.execute('SELECT COUNT(*) FROM rejections WHERE pending=1').fetchone()[0] >= self.MAX_PENDING:
                    raise RejectionStoreUnavailable()
                if db.execute('SELECT COUNT(*) FROM rejections').fetchone()[0] >= self.MAX_RECORDS:
                    raise RejectionStoreUnavailable()
                db.execute('INSERT INTO rejections VALUES (?,?,1,0,0,NULL,?,?)', (scope, receipt, subject, authorization_order))
            self.wake.set()
        except Exception:
            self._fail()

    def _claim(self):
        with self._connect() as db:
            db.execute('BEGIN IMMEDIATE')
            row = db.execute('SELECT scope,receipt,attempts FROM rejections WHERE pending=1 AND next_attempt<=? ORDER BY next_attempt LIMIT 1', (time.time(),)).fetchone()
            if row is not None:
                # Lease claims so another local process can recover after a crash.
                db.execute('UPDATE rejections SET next_attempt=?,attempts=attempts+1 WHERE scope=?', (time.time()+30, row[0]))
            return row

    def _run(self):
        while True:
            self.wake.wait(1)
            self.wake.clear()
            try:
                row = self._claim()
                if row is None:
                    continue
                scope, receipt, attempts = row
                try:
                    accepted = self.deliver('key-rejected', {'rejection_receipt': receipt}).get('recorded') is True
                except Exception:
                    accepted = False
                with self._connect() as db:
                    if accepted:
                        db.execute('UPDATE rejections SET pending=0,acknowledged_at=? WHERE scope=? AND receipt=?', (time.time(), scope, receipt))
                    else:
                        db.execute('UPDATE rejections SET next_attempt=? WHERE scope=? AND receipt=?', (time.time()+min(30, 2**min(attempts, 5)), scope, receipt))
            except Exception:
                # Storage failures disable this authority; never silently discard a receipt.
                try:
                    self._fail()
                except RejectionStoreUnavailable:
                    pass
