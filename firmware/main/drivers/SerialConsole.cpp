#include "SerialConsole.hpp"

#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "SerialConsole";

SerialConsole::SerialConsole(uart_port_t uartPort)
    : uartPort(uartPort) {
}

void SerialConsole::addCommand(const char* name, const char* description, CommandHandler handler) {
    commands.push_back({name, description, std::move(handler), nullptr});
}

void SerialConsole::addArgumentCommand(const char* name, const char* description, ArgumentHandler handler) {
    commands.push_back({name, description, nullptr, std::move(handler)});
}

esp_err_t SerialConsole::begin() {
    esp_err_t result = uart_driver_install(uartPort, RECEIVE_BUFFER_SIZE, 0, 0, nullptr, 0);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Installing the UART driver failed: %s", esp_err_to_name(result));

        return result;
    }

    // Route stdout through the driver too, so logs and command output share one path
    uart_vfs_dev_use_driver(uartPort);

    if (xTaskCreate(taskMain, "serial_console", TASK_STACK_BYTES, this, 2, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

uart_port_t SerialConsole::getUartPort() const {
    return uartPort;
}

void SerialConsole::taskMain(void* context) {
    static_cast<SerialConsole*>(context)->run();
}

void SerialConsole::run() {
    std::string line;

    while (true) {
        uint8_t character = 0;

        if (uart_read_bytes(uartPort, &character, 1, portMAX_DELAY) != 1) {
            continue;
        }

        // A line ends at CR or LF; empty lines are ignored
        if (character == '\r' || character == '\n') {
            if (!line.empty()) {
                handleLine(line);
                line.clear();
            }

            continue;
        }

        // Opening the port on the PC can put a junk byte on the line; keep printable ASCII only
        bool isPrintable = character >= 0x20 && character <= 0x7E;

        if (isPrintable && line.size() < MAX_LINE_LENGTH) {
            line.push_back(static_cast<char>(character));
        }
    }
}

void SerialConsole::handleLine(const std::string& line) {
    for (const Command& command : commands) {
        if (command.argumentHandler != nullptr) {
            // The command word followed by a space; the arguments are what follows the spaces
            if (line.size() > command.name.size() && line.compare(0, command.name.size(), command.name) == 0 && line[command.name.size()] == ' ') {
                size_t argumentsStart = line.find_first_not_of(' ', command.name.size());
                command.argumentHandler(argumentsStart == std::string::npos ? std::string() : line.substr(argumentsStart));

                return;
            }
        } else if (command.name == line) {
            command.handler();

            return;
        }
    }

    // "help" is built in, so every console can list its commands
    if (line == HELP_COMMAND) {
        for (const Command& command : commands) {
            ESP_LOGI(TAG, "%-24s %s", command.name.c_str(), command.description.c_str());
        }

        return;
    }

    ESP_LOGW(TAG, "Unknown command \"%s\", type %s", line.c_str(), HELP_COMMAND);
}
