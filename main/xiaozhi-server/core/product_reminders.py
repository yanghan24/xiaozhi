"""Single reminders. The model cannot confirm an operation on the speaker's behalf."""
import re
import threading
import time
import uuid
from datetime import datetime, timedelta, timezone


BEIJING = timezone(timedelta(hours=8))
NUMBER = r"[0-9零〇一二两三四五六七八九十]{1,4}"
DAY = r"(今天|明天|后天|下(?:周|星期)[一二三四五六日天]|(?:\d{4}年)?\d{1,2}月\d{1,2}[日号]|\d{4}-\d{1,2}-\d{1,2})"
ABSOLUTE_TIME = DAY + rf"(凌晨|早上|上午|中午|下午|晚上)?({NUMBER})(?:[:：]([0-9]{{2}})|[点时](?:({NUMBER})分|(?P<half>半)|整)?)"
RELATIVE_TIME = rf"(?:({NUMBER})(分钟|小时)|半个?小时)(?:之)?后"
TIME_HELP = "请说清日期、北京时间和事项，例如，明天下午三点提醒我开会，或者十分钟后提醒我喝水。"


def number(text):
    if text.isdecimal():
        return int(text)
    digits = dict(zip("零一二三四五六七八九", range(10)))
    digits.update({"两": 2, "〇": 0})
    if text in digits:
        return digits[text]
    tens = re.fullmatch(r"([一二两三四五六七八九]?)十([一二三四五六七八九]?)", text)
    if tens:
        return digits.get(tens[1], 1) * 10 + digits.get(tens[2], 0)
    raise ValueError()


def due_time(text, now):
    text = re.sub(r"\s", "", text).removeprefix("北京时间")
    if text in ("半小时后", "半个小时后"):
        return now + timedelta(minutes=30)
    relative = re.fullmatch(rf"({NUMBER})(分钟|小时)(?:之)?后", text)
    if relative:
        value = number(relative[1])
        if value < 1:
            raise ValueError()
        return now + timedelta(minutes=value * (60 if relative[2] == "小时" else 1))
    match = re.fullmatch(ABSOLUTE_TIME, text)
    if not match:
        raise ValueError()
    day, period, hours, minutes, spoken_minutes, half = match.groups()
    if day in ("今天", "明天", "后天"):
        date = now.date() + timedelta(days=("今天", "明天", "后天").index(day))
    elif day.startswith("下"):
        weekday = "一二三四五六日".index(day[-1].replace("天", "日"))
        date = now.date() + timedelta(days=7 - now.weekday() + weekday)
    else:
        date_parts = list(map(int, re.findall(r"\d+", day)))
        if len(date_parts) == 2:
            date_parts.insert(0, now.year)
        date = datetime(*date_parts).date()
    hour = number(hours)
    minute = 30 if half else number(minutes or spoken_minutes or "0")
    if period:
        if not 1 <= hour <= 12:
            raise ValueError()
        if period == "中午" and hour not in (11, 12, 1):
            raise ValueError()
        if hour == 12 and period in ("早上", "上午", "下午", "晚上"):
            raise ValueError()
        if period in ("下午", "晚上") or (period == "中午" and hour in (1, 12)):
            hour = hour % 12 + 12
        elif hour == 12 and period == "凌晨":
            hour = 0
    elif not minutes and 1 <= hour <= 12:
        raise ValueError()  # "明天三点" could be 03:00 or 15:00.
    result = datetime(date.year, date.month, date.day, hour, minute, tzinfo=BEIJING)
    if result <= now:
        raise ValueError()
    return result


def spoken(item):
    moment = datetime.fromtimestamp(item["due_at_ms"] / 1000, BEIJING)
    return f"北京时间{moment.year}年{moment.month}月{moment.day}日{moment:%H点%M分}，{item['title']}"


class ReminderConversation:
    def __init__(self):
        self.pending = None
        self.lock = threading.Lock()

    def reply(self, query, request, guard, now=None):
        if not isinstance(query, str):
            return None
        now = now or datetime.now(BEIJING)
        text = query.strip().strip("，。？！!?").strip()
        with self.lock:
            guard()
            pending, self.pending = self.pending, None
        if pending and time.monotonic() < pending[0]:
            command = pending[1]
            expected = {"create": "确认保存", "update": "确认修改", "cancel": "确认取消"}
            if text in ("确认", "是的", "确定", expected[command["action"]]):
                guard()
                result = request(command)
                if result is None:
                    return "暂时无法确认执行结果，请重新查询日程。"
                return {"saved": "日程已保存。", "cancelled": "这项日程已取消。"}.get(
                            result.get("status"), "这项日程状态已变化，请重新查询。")
            if text in ("不", "不确认", "取消", "不用了", "算了"):
                return "已放弃这次操作。"
            if text.startswith("确认"):
                return "这次确认与复述的操作不同，已放弃操作。"
        if not re.search(r"日程|提醒我|提醒事项", text):
            return None
        text = re.sub(r"^(?:请|麻烦)?(?:帮我)?", "", text).strip()
        listing = re.fullmatch(rf"(?:(?:查(?:询|看)?|列出|看看|我的|我有(?:哪些|什么))(?:一下)?(?:我的)?(?:日程|提醒事项)(?:有哪些)?|(?:日程|提醒事项)(?:列表|有哪些))(?:第({NUMBER})页)?", text)
        if listing:
            try:
                page = number(listing[1] or "1")
            except ValueError:
                return "请说查询日程第几页。"
            if page < 1:
                return "页码从第一页开始。"
            offset = (page - 1) * 5
            result = request({"action": "list", "offset": offset})
            if result is None:
                return "暂时无法读取日程，请稍后再试。"
            lines = [f"第{i}项，{spoken(item)}" for i, item in enumerate(result["items"], offset + 1)]
            if result.get("more"):
                lines.append(f"还可以说查询日程第{page + 1}页。")
            return "；".join(lines) or "这一页没有待办日程。"
        edit = re.fullmatch(rf"(取消|删除|修改|更改)(?:我的)?(?:日程第?|第)({NUMBER})(?:项|条|个)?(?:日程)?(?:[，, ]*(?:改为|改到|改成|为|到)?(.+))?", text)
        if edit:
            try:
                index = number(edit[2])
            except ValueError:
                return "请说出日程的序号。"
            result = request({"action": "list", "offset": max(0, index - 1)})
            if result is None:
                return "暂时无法读取日程，请稍后再试。"
            items = result["items"]
            if not items or index < 1:
                return "没有这个序号的日程，请先说查询日程。"
            item = items[0]
            command = {"id": item["id"], "version": item["version"]}
            if edit[1] in ("取消", "删除"):
                if edit[3]:
                    return "要取消一项，请说取消第几项日程。"
                command["action"] = "cancel"
                prompt = "取消" + spoken(item)
            else:
                parts = re.split(r"[，,]?提醒我[，,]?", edit[3] or "", maxsplit=1)
                try:
                    moment = due_time(parts[0], now)
                except ValueError:
                    return "请说修改第几项日程，改到具体日期时间。"
                command.update(action="update", title=parts[1].strip() if len(parts) == 2 else item["title"],
                               due_at_ms=int(moment.timestamp() * 1000))
                prompt = f"把{spoken(item)}修改为{spoken(command)}"
        else:
            if "提醒我" not in text and not re.match(r"(?:添加|新建|创建|设置|查询|查看|取消|删除|修改|更改)", text):
                return None
            text = re.sub(r"^(?:添加|新建|创建|设置)(?:一个|一条|一项)?日程[，,：: ]*", "", text)
            parts = re.fullmatch(r"(.+?)[，, ]*提醒我[，, ]*(.+)", text)
            if parts:
                time_text, title = parts.groups()
            else:
                if re.fullmatch(rf"提醒我(?:在)?(?:{RELATIVE_TIME}|{ABSOLUTE_TIME})", text):
                    return TIME_HELP
                prefix = re.fullmatch(rf"提醒我(?:在)?(?P<when>{RELATIVE_TIME}|{ABSOLUTE_TIME})[，, ]*(?P<title>.+)", text)
                if not prefix:
                    return TIME_HELP + "也可以说查询日程、取消第一项日程或修改第一项日程。"
                time_text, title = prefix["when"], prefix["title"]
            try:
                moment = due_time(time_text, now)
            except ValueError:
                return TIME_HELP
            command = {"action": "create", "id": uuid.uuid4().hex, "title": title.strip(),
                       "due_at_ms": int(moment.timestamp() * 1000)}
            prompt = "创建日程，" + spoken(command)
        if "title" in command and (not 1 <= len(command["title"].encode("utf-16-le")) // 2 <= 80
                                   or any(ord(c) < 32 for c in command["title"])):
            return "请将事项说得简短一些，最多八十个字。"
        with self.lock:
            guard()
            self.pending = (time.monotonic() + 90, command)
        return prompt + "。要执行请说确认。"
