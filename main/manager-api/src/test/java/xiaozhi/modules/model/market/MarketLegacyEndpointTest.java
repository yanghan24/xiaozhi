package xiaozhi.modules.model.market;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import org.junit.jupiter.api.Test;
import xiaozhi.modules.model.controller.ModelController;
import xiaozhi.modules.model.service.*;
import xiaozhi.modules.config.service.ConfigService;
import xiaozhi.modules.agent.service.AgentTemplateService;
import xiaozhi.modules.timbre.service.TimbreService;

class MarketLegacyEndpointTest {
    @Test void genericModelMutationsCannotBypassMarketSelectionAndCacheValidation() {
        var models=mock(ModelConfigService.class);var config=mock(ConfigService.class);
        var templates=mock(AgentTemplateService.class);
        var controller=new ModelController(mock(ModelProviderService.class),mock(TimbreService.class),models,config,templates,mock(MarketModelBindings.class));
        assertNotEquals(0,controller.enableModelConfig("MARKET_LLM",1).getCode());
        assertNotEquals(0,controller.setDefaultModel("MARKET_ASR").getCode());
        verifyNoInteractions(models,config,templates,mock(MarketModelBindings.class));
    }
}
