#include "SdoClient.hpp"

#include <chrono>

SdoClient::SdoClient(CanPort& bus, uint8_t nodeId, int timeoutMs)
    : bus(bus)
    , nodeId(nodeId)
    , timeoutMs(timeoutMs) {
}

SdoResult SdoClient::read(uint16_t index, uint8_t subIndex, uint32_t& value) {
    // Initiate upload request: index little-endian, sub-index
    uint8_t request[8] = {COMMAND_UPLOAD_REQUEST, static_cast<uint8_t>(index), static_cast<uint8_t>(index >> 8), subIndex, 0, 0, 0, 0};
    uint8_t response[8] = {};

    SdoResult result = transfer(request, response, COMMAND_UPLOAD_RESPONSE, true);

    if (result != SdoResult::OK) {
        return result;
    }

    // Only expedited responses carry the data here; a segmented upload (longer objects) is not handled yet
    uint8_t command = response[0];

    if ((command & FLAG_EXPEDITED) == 0) {
        return SdoResult::UNEXPECTED_RESPONSE;
    }

    // Bits 3-2 hold the number of unused bytes when the size is indicated; otherwise all four count
    uint8_t unusedBytes = (command & FLAG_SIZE_INDICATED) ? ((command >> 2) & 0x03) : 0;
    uint8_t sizeBytes = 4 - unusedBytes;

    value = 0;

    for (uint8_t byteIndex = 0; byteIndex < sizeBytes; byteIndex++) {
        value |= static_cast<uint32_t>(response[4 + byteIndex]) << (8 * byteIndex);
    }

    return SdoResult::OK;
}

SdoResult SdoClient::write(uint16_t index, uint8_t subIndex, uint32_t value, uint8_t sizeBytes) {
    if (sizeBytes < 1 || sizeBytes > 4) {
        return SdoResult::UNEXPECTED_RESPONSE;
    }

    // Expedited download with size: 0x23 (4 bytes), 0x27 (3), 0x2B (2), 0x2F (1)
    uint8_t command = COMMAND_DOWNLOAD_REQUEST | FLAG_EXPEDITED | FLAG_SIZE_INDICATED | ((4 - sizeBytes) << 2);
    uint8_t request[8] = {command,
                          static_cast<uint8_t>(index),
                          static_cast<uint8_t>(index >> 8),
                          subIndex,
                          static_cast<uint8_t>(value),
                          static_cast<uint8_t>(value >> 8),
                          static_cast<uint8_t>(value >> 16),
                          static_cast<uint8_t>(value >> 24)};
    uint8_t response[8] = {};

    return transfer(request, response, COMMAND_DOWNLOAD_RESPONSE, true);
}

SdoResult SdoClient::read(const ObjectEntry& entry, uint32_t& value) {
    return read(entry.index, entry.subIndex, value);
}

SdoResult SdoClient::readSigned(const ObjectEntry& entry, int32_t& value) {
    uint32_t raw = 0;
    SdoResult result = read(entry, raw);

    if (result != SdoResult::OK) {
        return result;
    }

    // Sign-extend from the object's size
    if (entry.sizeBytes == 1) {
        value = static_cast<int8_t>(raw);
    } else if (entry.sizeBytes == 2) {
        value = static_cast<int16_t>(raw);
    } else {
        value = static_cast<int32_t>(raw);
    }

    return SdoResult::OK;
}

SdoResult SdoClient::write(const ObjectEntry& entry, uint32_t value) {
    return write(entry.index, entry.subIndex, value, entry.sizeBytes);
}

SdoResult SdoClient::readString(uint16_t index, uint8_t subIndex, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) {
        return SdoResult::UNEXPECTED_RESPONSE;
    }

    size_t length = 0;
    buffer[0] = '\0';

    // Appends received bytes, keeping room for the terminator; whatever doesn't fit is dropped
    auto append = [&](const uint8_t* data, size_t count) {
        for (size_t byteIndex = 0; byteIndex < count && length < bufferSize - 1; byteIndex++) {
            buffer[length++] = static_cast<char>(data[byteIndex]);
        }

        buffer[length] = '\0';
    };

    // Initiate upload; the node answers expedited for up to 4 bytes, otherwise segmented
    uint8_t request[8] = {COMMAND_UPLOAD_REQUEST, static_cast<uint8_t>(index), static_cast<uint8_t>(index >> 8), subIndex, 0, 0, 0, 0};
    uint8_t response[8] = {};
    SdoResult result = transfer(request, response, COMMAND_UPLOAD_RESPONSE, true);

    if (result != SdoResult::OK) {
        return result;
    }

    if (response[0] & FLAG_EXPEDITED) {
        uint8_t unusedBytes = (response[0] & FLAG_SIZE_INDICATED) ? ((response[0] >> 2) & 0x03) : 0;
        append(&response[4], 4 - unusedBytes);

        return SdoResult::OK;
    }

    // Segments echo our toggle bit and carry up to 7 bytes; bits 3-1 hold the unused byte count (CiA 301 §7.2.4.3.5)
    uint8_t toggle = 0;

    for (int segment = 0; segment < MAX_SEGMENTS; segment++) {
        uint8_t segmentRequest[8] = {static_cast<uint8_t>(COMMAND_UPLOAD_SEGMENT_REQUEST | toggle), 0, 0, 0, 0, 0, 0, 0};
        result = transfer(segmentRequest, response, COMMAND_UPLOAD_SEGMENT_RESPONSE, false);

        if (result != SdoResult::OK) {
            // An abort already ended the transfer on the node; otherwise tell it we gave up
            if (result != SdoResult::ABORTED) {
                sendAbort(index, subIndex, ABORT_GENERAL);
            }

            return result;
        }

        if ((response[0] & FLAG_TOGGLE) != toggle) {
            sendAbort(index, subIndex, ABORT_TOGGLE_BIT);

            return SdoResult::UNEXPECTED_RESPONSE;
        }

        uint8_t unusedBytes = (response[0] >> 1) & 0x07;
        append(&response[1], 7 - unusedBytes);

        if (response[0] & FLAG_LAST_SEGMENT) {
            return SdoResult::OK;
        }

        toggle ^= FLAG_TOGGLE;
    }

    // Too many segments for any real string
    sendAbort(index, subIndex, ABORT_GENERAL);

    return SdoResult::UNEXPECTED_RESPONSE;
}

SdoResult SdoClient::readString(const ObjectEntry& entry, char* buffer, size_t bufferSize) {
    return readString(entry.index, entry.subIndex, buffer, bufferSize);
}

void SdoClient::sendAbort(uint16_t index, uint8_t subIndex, uint32_t abortCode) {
    CanFrame frame;
    frame.id = SDO_REQUEST_COB_ID + nodeId;
    frame.length = 8;
    frame.data[0] = COMMAND_ABORT;
    frame.data[1] = static_cast<uint8_t>(index);
    frame.data[2] = static_cast<uint8_t>(index >> 8);
    frame.data[3] = subIndex;
    frame.data[4] = static_cast<uint8_t>(abortCode);
    frame.data[5] = static_cast<uint8_t>(abortCode >> 8);
    frame.data[6] = static_cast<uint8_t>(abortCode >> 16);
    frame.data[7] = static_cast<uint8_t>(abortCode >> 24);

    bus.send(frame, timeoutMs);
}

bool SdoClient::sendNmtToAll(uint8_t command) {
    CanFrame frame;
    frame.id = NMT_COB_ID;
    frame.length = 2;
    frame.data[0] = command;
    frame.data[1] = 0; // All nodes

    return bus.send(frame, timeoutMs);
}

void SdoClient::setNodeId(uint8_t newNodeId) {
    nodeId = newNodeId;
}

uint8_t SdoClient::getNodeId() const {
    return nodeId;
}

void SdoClient::setTimeoutMs(int newTimeoutMs) {
    timeoutMs = newTimeoutMs;
}

bool SdoClient::sendNmt(uint8_t command) {
    CanFrame frame;
    frame.id = NMT_COB_ID;
    frame.length = 2;
    frame.data[0] = command;
    frame.data[1] = nodeId;

    return bus.send(frame, timeoutMs);
}

SdoResult SdoClient::transfer(const uint8_t request[8], uint8_t response[8], uint8_t expectedResponse, bool shouldMatchObject) {
    // Hand over anything that arrived since the last transfer (EMCY), so an old response can't be taken for this one
    CanFrame pending;

    while (bus.receive(pending, 0)) {
        handleOtherFrame(pending);
    }

    CanFrame frame;
    frame.id = SDO_REQUEST_COB_ID + nodeId;
    frame.length = 8;

    for (int byteIndex = 0; byteIndex < 8; byteIndex++) {
        frame.data[byteIndex] = request[byteIndex];
    }

    if (!bus.send(frame, timeoutMs)) {
        return SdoResult::SEND_FAILED;
    }

    // Wait for a response of the expected type (or an abort) from this node for the same object
    // std::chrono rather than esp_timer, so protocol code also builds on a PC
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (true) {
        int64_t remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();

        if (remainingMs <= 0) {
            return SdoResult::TIMEOUT;
        }

        CanFrame received;

        if (!bus.receive(received, static_cast<int>(remainingMs))) {
            return SdoResult::TIMEOUT;
        }

        bool isFromNode = received.id == SDO_RESPONSE_COB_ID + nodeId && received.length == 8;
        bool isSameObject = !shouldMatchObject || (received.data[1] == request[1] && received.data[2] == request[2] && received.data[3] == request[3]);
        uint8_t specifier = received.data[0] & COMMAND_SPECIFIER_MASK;
        bool isExpectedType = specifier == expectedResponse || received.data[0] == COMMAND_ABORT;

        if (!isFromNode || !isSameObject || !isExpectedType) {
            handleOtherFrame(received);

            continue;
        }

        for (int byteIndex = 0; byteIndex < 8; byteIndex++) {
            response[byteIndex] = received.data[byteIndex];
        }

        if (response[0] == COMMAND_ABORT) {
            lastAbortCode = static_cast<uint32_t>(response[4]) | (static_cast<uint32_t>(response[5]) << 8) | (static_cast<uint32_t>(response[6]) << 16) | (static_cast<uint32_t>(response[7]) << 24);

            return SdoResult::ABORTED;
        }

        return SdoResult::OK;
    }
}

void SdoClient::setOtherFrameHandler(std::function<void(const CanFrame&)> handler) {
    otherFrameHandler = std::move(handler);
}

void SdoClient::handleOtherFrame(const CanFrame& frame) {
    if (otherFrameHandler) {
        otherFrameHandler(frame);
    }
}

const char* SdoClient::getResultName(SdoResult result) {
    switch (result) {
        case SdoResult::OK:
            return "ok";

        case SdoResult::SEND_FAILED:
            return "send failed (no ACK)";

        case SdoResult::TIMEOUT:
            return "no response";

        case SdoResult::ABORTED:
            return "aborted";

        case SdoResult::UNEXPECTED_RESPONSE:
            return "unexpected response";
    }

    return "unknown";
}

const char* SdoClient::getAbortCodeDescription(uint32_t abortCode) {
    // CiA 301 SDO abort codes
    switch (abortCode) {
        case 0x05030000:
            return "toggle bit not alternated";

        case 0x05040000:
            return "SDO protocol timed out";

        case 0x05040001:
            return "invalid command specifier";

        case 0x06010000:
            return "unsupported access to an object";

        case 0x06010001:
            return "attempt to read a write-only object";

        case 0x06010002:
            return "attempt to write a read-only object";

        case 0x06020000:
            return "object does not exist";

        case 0x06040041:
            return "object cannot be mapped to a PDO";

        case 0x06060000:
            return "access failed due to a hardware error";

        case 0x06070010:
            return "data type or length does not match";

        case 0x06090011:
            return "sub-index does not exist";

        case 0x06090030:
            return "invalid value";

        case 0x06090031:
            return "value too high";

        case 0x06090032:
            return "value too low";

        case 0x08000000:
            return "general error";

        case 0x08000020:
            return "data cannot be stored or transferred";

        case 0x08000021:
            return "data cannot be transferred because of local control";

        case 0x08000022:
            return "data cannot be transferred in the present device state";
    }

    return "unknown abort code";
}
