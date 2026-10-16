#include "cec_phy.h"
#include "esp_err.h"
#include "esp_log.h"
#include "net.h"
#include "pulse8.h"

static const char *TAG = "plum_cec";

void app_main(void)
{
    ESP_LOGI(TAG, "Plum-CEC firmware starting");
    ESP_ERROR_CHECK(cec_phy_init());
    ESP_ERROR_CHECK(pulse8_init());
    ESP_ERROR_CHECK(net_init());
}
