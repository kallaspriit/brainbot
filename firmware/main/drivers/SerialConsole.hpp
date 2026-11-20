#pragma once

#include <functional>
#include <string>
#include <vector>

#include "driver/uart.h"

/**
 * Line-based command reader on the serial console (the USB-UART port).
 *
 * Installs the UART driver on the console UART so the firmware can read what the PC sends, and runs registered
 * handlers by command name in its own low-priority task. Log output keeps working; it goes through the same driver.
 */
class SerialConsole {
  public:
    using CommandHandler = std::function<void()>;
    using ArgumentHandler = std::function<void(const std::string& arguments)>;

    /**
     * Creates the console reader. Call begin() after registering commands.
     *
     * @param uartPort The console UART (CONFIG_ESP_CONSOLE_UART_NUM)
     */
    explicit SerialConsole(uart_port_t uartPort);

    /**
     * Registers a command. `help` is built in and lists them.
     *
     * @param name Command line, matched exactly against a received line
     * @param description One line shown by `help`
     * @param handler Function to run in the console task
     */
    void addCommand(const char* name, const char* description, CommandHandler handler);

    /**
     * Registers a command that takes arguments: a line that is the name followed by a space runs it with the rest of
     * the line, e.g. `sdo r 2000 07`.
     *
     * @param name Command word, matched against the start of a received line
     * @param description One line shown by `help`
     * @param handler Function to run in the console task, given the text after the name without leading spaces
     */
    void addArgumentCommand(const char* name, const char* description, ArgumentHandler handler);

    /**
     * Installs the UART driver and starts the reader task.
     *
     * @return ESP_OK on success, or the driver error
     */
    esp_err_t begin();

    /**
     * Returns the UART port, for commands that write binary data.
     *
     * @return The console UART
     */
    uart_port_t getUartPort() const;

  private:
    struct Command {
        std::string name;                // Command line, or the command word when it takes arguments
        std::string description;         // Shown by help
        CommandHandler handler;          // What to run, for a command without arguments
        ArgumentHandler argumentHandler; // What to run with the rest of the line, for a command with arguments
    };

    uart_port_t uartPort;
    std::vector<Command> commands;

    /**
     * Task entry point.
     *
     * @param context The SerialConsole instance
     */
    static void taskMain(void* context);

    /**
     * Reads lines and runs matching commands, forever.
     */
    void run();

    /**
     * Runs the command matching a received line, or lists the commands.
     *
     * @param line The received line without line ending
     */
    void handleLine(const std::string& line);

    // Constants
    static constexpr int RECEIVE_BUFFER_SIZE = 256;     // UART driver receive buffer
    static constexpr uint32_t TASK_STACK_BYTES = 16384; // Commands run here; lv_snapshot renders the whole screen on this stack (6 KB overflowed on the Position screen)
    static constexpr size_t MAX_LINE_LENGTH = 64;       // Longer lines are discarded
    static constexpr const char* HELP_COMMAND = "help";
};
