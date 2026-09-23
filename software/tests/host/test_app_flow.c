#include <stdbool.h>

#include "app_flow.h"
#include "telemetry.h"
#include "test_framework.h"

static void test_app_flow_decide(void)
{
    CHECK_EQ_INT(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_POWER_ON, false));
    CHECK_EQ_INT(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_POWER_ON, true));
    CHECK_EQ_INT(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_TIMER, false));
    CHECK_EQ_INT(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_TIMER, true));
    CHECK_EQ_INT(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_UNKNOWN, false));
    CHECK_EQ_INT(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_UNKNOWN, true));

    CHECK_EQ_INT(APP_ACTION_LIGHTNING, app_flow_decide(APP_WAKE_LIGHTNING_IRQ, true));
    CHECK_EQ_INT(APP_ACTION_SLEEP, app_flow_decide(APP_WAKE_LIGHTNING_IRQ, false));
}

static void test_app_flow_transport_from_j4(void)
{
    /* Wi-Fi active high */
    CHECK_EQ_INT(TRANSPORT_MODE_WIFI, app_flow_transport_from_j4(true, true));
    CHECK_EQ_INT(TRANSPORT_MODE_LORA, app_flow_transport_from_j4(false, true));
    /* Wi-Fi active low */
    CHECK_EQ_INT(TRANSPORT_MODE_LORA, app_flow_transport_from_j4(true, false));
    CHECK_EQ_INT(TRANSPORT_MODE_WIFI, app_flow_transport_from_j4(false, false));
}

int main(void)
{
    test_app_flow_decide();
    test_app_flow_transport_from_j4();
    TEST_SUMMARY_AND_RETURN();
}
