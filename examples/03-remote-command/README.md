# 03 — Remote Command

[English](README.md) | [Tiếng Việt](README_vi.md)

Receive remote commands from the cloud, execute hardware actions, and report status back.

## Hardware Requirements
Devkit board + 1 LED on GPIO2 (adjust `LED_GPIO` for your specific board). Works without an LED by observing logs.

## Protocol Structure (Handled automatically by SDK)
```jsonc
// cloud → device
{"type":"command","commandId":91,"action":"led","params":{"on":true}}
// device → cloud
{"type":"command_ack","commandId":91,"status":"ok","message":"LED is turned on","result":{"on":true}}
```

## Registering a New Command Handler
```c
static esp_err_t cmd_xyz(cJSON *params, char *result, size_t result_len,
                         char *msg, size_t msg_len)
{
    // ... execute hardware actuation ...
    snprintf(result, result_len, "{\"count\":%d}", n); // Populates the "result" field
    snprintf(msg, msg_len, "done");                    // Populates the "message" field
    return ESP_OK;                                     // Non-OK returns status "error"
}

innoedge_register_command("xyz", cmd_xyz);   // Single line registration
```

## Three Essential Rules

1. **Never block the WebSocket task.** Handlers execute on the WebSocket thread. For long-running operations (>1 second, such as dispensing 50 items or waiting for sensors), spawn a FreeRTOS task via `xTaskCreate` and return `ESP_OK` immediately.
2. **Never call `esp_restart()` directly inside a handler.** Calling restart before the ACK frame is flushed causes the cloud to assume a network timeout and resend the command. Always call `innoedge_reboot_after_ack()`.
3. **`action` must be a string literal.** The command registry stores pointers without string duplication.

## Idempotency: Never Dispense Twice
Cloud servers frequently retry commands when cellular signals jitter. The SDK stores a persistent **high-watermark `commandId` in NVS**: any command where `commandId ≤ watermark` is recognized as already executed → immediately ACKed with `{"status":"ok","message":"duplicate"}`, **without invoking the hardware handler a second time**.

The watermark persists across sudden power cuts and reboots. If power is lost mid-dispense, subsequent retry commands upon reboot will not dispense twice.

*Verification:* Send the same `commandId` twice via the Web Dashboard → The second execution returns duplicate status and does not actuate the hardware.

## Troubleshooting
| Symptom | Cause & Solution |
|---|---|
| `unknown action` | Forgot `innoedge_register_command`, or action name mismatch. |
| Command runs but cloud reports timeout | Handler blocked the WebSocket task too long → offload to a separate FreeRTOS task. |
| Command executes twice | Manually calling dispatcher instead of letting the SDK handle it. |
| Registry full | Exceeded maximum number of actions — increase `GTEK_CMD_REGISTRY_MAX`. |
