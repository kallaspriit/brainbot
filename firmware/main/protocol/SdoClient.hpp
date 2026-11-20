#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "drivers/CanPort.hpp"
#include "protocol/Cia402.hpp"

/**
 * Outcome of an SDO transfer.
 */
enum class SdoResult {
    OK,                  // Transfer completed
    SEND_FAILED,         // Request was not acknowledged on the bus (no node, wiring, bitrate)
    TIMEOUT,             // No response within the timeout
    ABORTED,             // Node answered with an SDO abort; see the abort code
    UNEXPECTED_RESPONSE, // Node answered with something we don't handle, or broke the segmented protocol
};

/**
 * Minimal CANopen SDO client: expedited upload (read) and download (write) of up to 4 bytes, segmented upload of
 * strings, and NMT commands.
 *
 * One request at a time. Not thread-safe: use from a single task.
 */
class SdoClient {
  public:
    /**
     * Creates an SDO client for one node.
     *
     * @param bus CAN bus to use
     * @param nodeId CANopen node ID (1-127)
     * @param timeoutMs Time to wait for each response
     */
    SdoClient(CanPort& bus, uint8_t nodeId, int timeoutMs);

    /**
     * Reads an object with an expedited SDO upload.
     *
     * @param index Object index
     * @param subIndex Object sub-index
     * @param value Receives the value, zero-extended to 32 bits
     * @return Transfer result; lastAbortCode holds the code if ABORTED
     */
    SdoResult read(uint16_t index, uint8_t subIndex, uint32_t& value);

    /**
     * Writes an object with an expedited SDO download.
     *
     * @param index Object index
     * @param subIndex Object sub-index
     * @param value Value to write, little-endian on the wire
     * @param sizeBytes Object size in bytes (1-4)
     * @return Transfer result; lastAbortCode holds the code if ABORTED
     */
    SdoResult write(uint16_t index, uint8_t subIndex, uint32_t value, uint8_t sizeBytes);

    /**
     * Reads an object table entry with an expedited SDO upload.
     *
     * @param entry Object to read
     * @param value Receives the value, zero-extended to 32 bits
     * @return Transfer result; lastAbortCode holds the code if ABORTED
     */
    SdoResult read(const ObjectEntry& entry, uint32_t& value);

    /**
     * Reads a signed object table entry, sign-extending from the entry's size (int16 current, int32 velocity).
     *
     * @param entry Object to read
     * @param value Receives the value; left unchanged unless the result is OK
     * @return Transfer result; lastAbortCode holds the code if ABORTED
     */
    SdoResult readSigned(const ObjectEntry& entry, int32_t& value);

    /**
     * Writes an object table entry with an expedited SDO download, using the entry's size.
     *
     * @param entry Object to write
     * @param value Value to write
     * @return Transfer result; lastAbortCode holds the code if ABORTED
     */
    SdoResult write(const ObjectEntry& entry, uint32_t value);

    /**
     * Reads a string object (VISIBLE_STRING, e.g. 1008 device name) with a segmented upload, or an expedited one if the
     * node answers that way for a short string.
     *
     * The text is always null-terminated. A longer string is cut to fit the buffer; the rest is still read so the
     * transfer ends cleanly.
     *
     * @param index Object index
     * @param subIndex Object sub-index
     * @param buffer Receives the text
     * @param bufferSize Size of the buffer, at least 1
     * @return Transfer result; lastAbortCode holds the code if ABORTED
     */
    SdoResult readString(uint16_t index, uint8_t subIndex, char* buffer, size_t bufferSize);

    /**
     * Reads a string object table entry; see readString(uint16_t, uint8_t, char*, size_t).
     *
     * @param entry Object to read
     * @param buffer Receives the text
     * @param bufferSize Size of the buffer, at least 1
     * @return Transfer result; lastAbortCode holds the code if ABORTED
     */
    SdoResult readString(const ObjectEntry& entry, char* buffer, size_t bufferSize);

    /**
     * Sends an NMT command to the node.
     *
     * @param command NMT command specifier (0x01 start, 0x80 pre-operational, 0x81 reset application)
     * @return True if the frame was sent
     */
    bool sendNmt(uint8_t command);

    /**
     * Sends an NMT command to every node (node ID 0 in the NMT frame).
     *
     * @param command NMT command specifier, e.g. NMT_ENTER_PRE_OPERATIONAL
     * @return True if the frame was acknowledged on the bus
     */
    bool sendNmtToAll(uint8_t command);

    /**
     * Talks to another node from now on, for finding a drive set to another node ID (D-026).
     *
     * @param newNodeId CANopen node ID (1-127)
     */
    void setNodeId(uint8_t newNodeId);

    /**
     * Returns the node the client talks to.
     *
     * @return Node ID
     */
    uint8_t getNodeId() const;

    /**
     * Changes how long to wait for a response; a node scan waits less than normal traffic.
     *
     * @param newTimeoutMs Timeout in ms
     */
    void setTimeoutMs(int newTimeoutMs);

    /**
     * Returns a short name for an SDO result.
     *
     * @param result The result
     * @return Name of the result
     */
    static const char* getResultName(SdoResult result);

    /**
     * Sets a handler for received frames that aren't the response being waited for, such as EMCY messages. Called from
     * the task using the client, during a transfer.
     *
     * @param handler Handler, or nullptr to drop such frames
     */
    void setOtherFrameHandler(std::function<void(const CanFrame&)> handler);

    /**
     * Returns the CiA 301 description of an SDO abort code.
     *
     * @param abortCode The abort code
     * @return Description, or "unknown abort code"
     */
    static const char* getAbortCodeDescription(uint32_t abortCode);

    // Last transfer
    uint32_t lastAbortCode = 0; // Abort code of the last ABORTED transfer

  private:
    CanPort& bus;
    uint8_t nodeId;
    int timeoutMs;
    std::function<void(const CanFrame&)> otherFrameHandler;

    /**
     * Passes a frame that isn't the awaited response to the handler, if there is one.
     *
     * @param frame The frame
     */
    void handleOtherFrame(const CanFrame& frame);

    /**
     * Sends a request and waits for the matching response from the node: the expected response type or an abort, for
     * the same object. Other frames, such as a late response to an earlier request, are skipped.
     *
     * @param request Request payload (8 bytes)
     * @param response Receives the response payload
     * @param expectedResponse Server command specifier expected back (COMMAND_UPLOAD_RESPONSE,
     * COMMAND_DOWNLOAD_RESPONSE or COMMAND_UPLOAD_SEGMENT_RESPONSE)
     * @param shouldMatchObject Whether the response must name the request's object; segment responses carry data there
     * @return Transfer result
     */
    SdoResult transfer(const uint8_t request[8], uint8_t response[8], uint8_t expectedResponse, bool shouldMatchObject);

    /**
     * Tells the node to give up a transfer we can't continue, so it doesn't wait for the next segment request.
     *
     * @param index Object index
     * @param subIndex Object sub-index
     * @param abortCode CiA 301 abort code
     */
    void sendAbort(uint16_t index, uint8_t subIndex, uint32_t abortCode);

    // Constants
    static constexpr uint32_t SDO_REQUEST_COB_ID = 0x600;  // Client to server
    static constexpr uint32_t SDO_RESPONSE_COB_ID = 0x580; // Server to client
  public:
    // NMT command specifiers (CiA 301)
    static constexpr uint8_t NMT_ENTER_PRE_OPERATIONAL = 0x80; // A node in Stopped ignores SDO until it gets this

  private:
    static constexpr uint32_t NMT_COB_ID = 0x000;

    // Command byte (CiA 301 §7.2.4.3); the top three bits are the command specifier
    static constexpr uint8_t COMMAND_SPECIFIER_MASK = 0xE0;
    static constexpr uint8_t COMMAND_UPLOAD_REQUEST = 0x40;          // Initiate upload (read)
    static constexpr uint8_t COMMAND_UPLOAD_RESPONSE = 0x40;         // Initiate upload response
    static constexpr uint8_t COMMAND_DOWNLOAD_REQUEST = 0x20;        // Initiate download (write)
    static constexpr uint8_t COMMAND_DOWNLOAD_RESPONSE = 0x60;       // Initiate download response
    static constexpr uint8_t COMMAND_UPLOAD_SEGMENT_REQUEST = 0x60;  // Upload segment, plus the toggle bit
    static constexpr uint8_t COMMAND_UPLOAD_SEGMENT_RESPONSE = 0x00; // Upload segment response
    static constexpr uint8_t COMMAND_ABORT = 0x80;                   // Abort transfer
    static constexpr uint8_t FLAG_EXPEDITED = 0x02;                  // e: data is in this frame
    static constexpr uint8_t FLAG_SIZE_INDICATED = 0x01;             // s: bits 3-2 hold the unused byte count
    static constexpr uint8_t FLAG_TOGGLE = 0x10;                     // t: alternates per segment, echoed by the node
    static constexpr uint8_t FLAG_LAST_SEGMENT = 0x01;               // c: no more segments follow

    // Abort codes we send, and the segmented upload limit
    static constexpr uint32_t ABORT_TOGGLE_BIT = 0x05030000; // Toggle bit not alternated
    static constexpr uint32_t ABORT_GENERAL = 0x08000000;    // General error
    static constexpr int MAX_SEGMENTS = 64;                  // 448 bytes, far above any string the drives have
};
