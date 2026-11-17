package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;
import java.util.Map;
import org.junit.jupiter.api.Test;

class CustomerProfileDisplayTest {
    @Test void matchesMarketNicknameInsteadOfGeneratedLoginName() {
        assertEquals("小筑", CustomerProfileDisplay.name(Map.of(
                "sub", "opaque-subject-123", "preferred_username", "wx_generated123",
                "name", "小筑", "nickname", "小筑")));
    }

    @Test void prefersNicknameWhenOtherProfileNamesDiffer() {
        assertEquals("集市昵称", CustomerProfileDisplay.name(Map.of(
                "preferred_username", "login123", "name", "其他名称", "nickname", "集市昵称")));
    }

    @Test void acceptsOidcDisplayNameWithoutSeparateNickname() {
        assertEquals("小筑", CustomerProfileDisplay.name(Map.of(
                "name", "小筑", "preferred_username", "wx_generated123")));
    }

    @Test void fallsBackToTheMarketLoginName() {
        assertEquals("market-login123", CustomerProfileDisplay.name(Map.of(
                "preferred_username", "market-login123")));
    }

    @Test void rejectsUnusableCandidatesBeforeTryingTheNextField() {
        assertAll(
                () -> assertEquals("可用名称", CustomerProfileDisplay.name(Map.of(
                        "nickname", "不可用\n名称", "name", "可用名称", "preferred_username", "login123"))),
                () -> assertEquals("login123", CustomerProfileDisplay.name(Map.of(
                        "nickname", "筑".repeat(86), "name", "  ", "preferred_username", "login123"))),
                () -> assertEquals("login123", CustomerProfileDisplay.name(Map.of(
                        "nickname", 123, "name", true, "preferred_username", "login123"))));
    }

    @Test void doesNotTurnTheSubjectIntoAUsername() {
        assertNull(CustomerProfileDisplay.name(Map.of("sub", "opaque-subject-123")));
        assertNull(CustomerProfileDisplay.name(Map.of("preferred_username", "bad\u0000name")));
    }
}
