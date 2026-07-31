#include "app_startup.h"

#include <stddef.h>

volatile App_Startup_Snapshot_t g_appStartup;

static uint8_t App_Startup_IsSingleResource(uint32_t resource)
{
    return ((resource != 0U) &&
            ((resource & (resource - 1U)) == 0U)) ? 1U : 0U;
}

void App_Startup_Init(uint32_t expected_mask)
{
    g_appStartup = (App_Startup_Snapshot_t){0};
    g_appStartup.expected_mask = expected_mask;
}

void App_Startup_Record(App_Startup_Resource_t resource,
                        App_Startup_Result_t result)
{
    uint32_t resource_mask = (uint32_t)resource;

    if ((App_Startup_IsSingleResource(resource_mask) == 0U) ||
        ((resource_mask & g_appStartup.expected_mask) == 0U))
    {
        return;
    }

    g_appStartup.attempted_mask |= resource_mask;
    g_appStartup.record_count++;

    if (result == APP_STARTUP_RESULT_OK)
    {
        g_appStartup.ready_mask |= resource_mask;
        g_appStartup.failed_mask &= ~resource_mask;
    }
    else
    {
        g_appStartup.ready_mask &= ~resource_mask;
        g_appStartup.failed_mask |= resource_mask;
        g_appStartup.failure_count++;
        g_appStartup.last_failed_resource = resource_mask;
        g_appStartup.last_failure_result = (uint32_t)result;

        if (g_appStartup.first_failed_resource == 0U)
        {
            g_appStartup.first_failed_resource = resource_mask;
            g_appStartup.first_failure_result = (uint32_t)result;
        }
    }

    g_appStartup.degraded =
        ((g_appStartup.failed_mask != 0U) ||
         ((g_appStartup.attempted_mask & g_appStartup.expected_mask) !=
          g_appStartup.expected_mask)) ? 1U : 0U;
}

uint8_t App_Startup_IsReady(App_Startup_Resource_t resource)
{
    uint32_t resource_mask = (uint32_t)resource;

    if (App_Startup_IsSingleResource(resource_mask) == 0U)
    {
        return 0U;
    }

    return ((g_appStartup.ready_mask & resource_mask) != 0U) ? 1U : 0U;
}

void App_Startup_GetSnapshot(App_Startup_Snapshot_t *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = (App_Startup_Snapshot_t)g_appStartup;
    }
}
