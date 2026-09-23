#pragma once

/* Minimal esp_err.h shim so pure-logic components compile on the host. */

typedef int esp_err_t;

#define ESP_OK               0
#define ESP_FAIL            -1
#define ESP_ERR_INVALID_ARG  -2
#define ESP_ERR_INVALID_SIZE -3
#define ESP_ERR_INVALID_STATE -4
#define ESP_ERR_TIMEOUT      -5
