#include "KnxTpUart.h"

KnxTpUart::KnxTpUart(TPUART_SERIAL_CLASS* sport, byte address[2]) {
    _serialport = sport;
    
    _individualAddress[0] = address[0];
    _individualAddress[1] = address[1];
    
    _listen_group_address_count = 0;
    _tg = new KnxTelegram();
    _tg_ptp = new KnxTelegram();
    _listen_to_broadcasts = false;
}

void KnxTpUart::setListenToBroadcasts(bool listen) {
    _listen_to_broadcasts = listen;
}

void KnxTpUart::uartReset() {
    byte sendByte = 0x01;
    _serialport->write(sendByte);
}

void KnxTpUart::uartStateRequest() {
    byte sendByte = 0x02;
    _serialport->write(sendByte);
}

void KnxTpUart::setIndividualAddress(byte address[2]) {
    _individualAddress[0] = address[0];
    _individualAddress[1] = address[1];
}

void KnxTpUart::getIndividualAddress(byte address[2]) {
    address[0] = _individualAddress[0];
    address[1] = _individualAddress[1];
}

KnxTpUartSerialEventType KnxTpUart::serialEvent() {
    while (_serialport->available() > 0) {
        checkErrors();
        
        int incomingByte = _serialport->peek();
        printByte(incomingByte);
        
        if (isKNXControlByte(incomingByte)) {
            bool interested = readKNXTelegram();
            if (interested) {
#if defined(TPUART_DEBUG)
                TPUART_DEBUG_PORT.println("Event KNX_TELEGRAM");
#endif
                return KNX_TELEGRAM;
            } else {
#if defined(TPUART_DEBUG)
                TPUART_DEBUG_PORT.println("Event IRRELEVANT_KNX_TELEGRAM");
#endif
                return IRRELEVANT_KNX_TELEGRAM;
            }
        } else if (incomingByte == TPUART_RESET_INDICATION_BYTE) {
            serialRead();
#if defined(TPUART_DEBUG)
            TPUART_DEBUG_PORT.println("Event TPUART_RESET_INDICATION");
#endif
            return TPUART_RESET_INDICATION;
        } else {
            serialRead();
#if defined(TPUART_DEBUG)
            TPUART_DEBUG_PORT.println("Event UNKNOWN");
#endif
            return UNKNOWN;
        }
    }
#if defined(TPUART_DEBUG)
    TPUART_DEBUG_PORT.println("Event UNKNOWN");
#endif
    return UNKNOWN;
}


bool KnxTpUart::isKNXControlByte(int b) {
    return ( (b | B00101100) == B10111100 ); // Ignore repeat flag and priority flag
}

void KnxTpUart::checkErrors() {
#if defined(TPUART_DEBUG)
#if defined(_SAM3XA_)  // For DUE
    if (USART1->US_CSR & US_CSR_OVRE) {
        TPUART_DEBUG_PORT.println("Overrun"); 
    }

    if (USART1->US_CSR & US_CSR_FRAME) {
        TPUART_DEBUG_PORT.println("Frame Error");
    }

    if (USART1->US_CSR & US_CSR_PARE) {
        TPUART_DEBUG_PORT.println("Parity Error");
    }
#elif defined(__AVR_ATmega168__) || defined(__AVR_ATmega328P__) // for UNO
    if (UCSR0A & B00010000) {
        TPUART_DEBUG_PORT.println("Frame Error"); 
    }
    
    if (UCSR0A & B00000100) {
        TPUART_DEBUG_PORT.println("Parity Error"); 
    }
#else
    if (UCSR1A & B00010000) {
        TPUART_DEBUG_PORT.println("Frame Error"); 
    }
    
    if (UCSR1A & B00000100) {
        TPUART_DEBUG_PORT.println("Parity Error"); 
    }
#endif
#endif
}

void KnxTpUart::printByte(int incomingByte) {
#if defined(TPUART_DEBUG)
    TPUART_DEBUG_PORT.print("Incoming Byte: ");
    TPUART_DEBUG_PORT.print(incomingByte, DEC);
    TPUART_DEBUG_PORT.print(" - ");
    TPUART_DEBUG_PORT.print(incomingByte, HEX);
    TPUART_DEBUG_PORT.print(" - ");
    TPUART_DEBUG_PORT.print(incomingByte, BIN);
    TPUART_DEBUG_PORT.println();
#endif
}

bool KnxTpUart::readKNXTelegram() {
    // Receive header
    for (int i = 0; i < 6; i++) {
        _tg->setBufferByte(i, serialRead());
    }

#if defined(TPUART_DEBUG)
    TPUART_DEBUG_PORT.print("Payload Length: ");
    TPUART_DEBUG_PORT.println(_tg->getPayloadLength());
#endif
    int bufpos = 6;
    for (int i = 0; i < _tg->getPayloadLength(); i++) {
        _tg->setBufferByte(bufpos, serialRead());
        bufpos++; 
    }

    // Checksum
    _tg->setBufferByte(bufpos, serialRead());

#if defined(TPUART_DEBUG)
    // Print the received telegram
    _tg->print(&TPUART_DEBUG_PORT);
#endif

    // get targetaddress if telegram
    byte target[2];
    _tg->getTarget(target);

    // Verify if we are interested in this message:
    // GroupAddress
    bool interestedGA = _tg->isTargetGroup() && isListeningToGroupAddress(target);
    
    // Physical address
    bool interestedPA = ((!_tg->isTargetGroup()) && target[0] == _individualAddress[0] && target[1] == _individualAddress[1]);
    
    // Broadcast (Programming Mode)
    bool interestedBC = (_listen_to_broadcasts && _tg->isBroadcast());

    TPUART_DEBUG_PORT.print("Interested GA: ");
    TPUART_DEBUG_PORT.println(interestedGA);
    TPUART_DEBUG_PORT.print("Interested PA: ");
    TPUART_DEBUG_PORT.println(interestedPA);
    TPUART_DEBUG_PORT.print("Interested BC: ");
    TPUART_DEBUG_PORT.println(interestedBC);
    
    TPUART_DEBUG_PORT.print("target: [0]=");
    TPUART_DEBUG_PORT.print(target[0]);
    TPUART_DEBUG_PORT.print(" [1]=");
    TPUART_DEBUG_PORT.print(target[1]);
    TPUART_DEBUG_PORT.println();

    bool interested = interestedGA || interestedPA ||interestedBC;

    if (interested) {
        sendAck();
    } else {
        sendNotAddressed();
    }

    if (_tg->getCommunicationType() == KNX_COMM_UCD) {
#if defined(TPUART_DEBUG)
      TPUART_DEBUG_PORT.println("UCD Telegram received");
#endif
    } else if (_tg->getCommunicationType() == KNX_COMM_NCD) {
#if defined(TPUART_DEBUG)
        TPUART_DEBUG_PORT.print("NCD Telegram ");
        TPUART_DEBUG_PORT.print(_tg->getSequenceNumber());
        TPUART_DEBUG_PORT.println(" received");
#endif
        if (interested) {
            sendNCDPosConfirm(_tg->getSequenceNumber(), PA_INTEGER(_tg->getSourceArea(), _tg->getSourceLine(), _tg->getSourceMember()));
        }
    }
    
    // Returns if we are interested in this diagram
    return interested;
}

KnxTelegram* KnxTpUart::getReceivedTelegram() {
    return _tg;
}

bool KnxTpUart::groupWriteBool(byte groupAddress[2], bool value) {
    int valueAsInt = 0;
    if (value) {
        valueAsInt = B00000001;
    }
    
    createKNXMessageFrame(2, KNX_COMMAND_WRITE, groupAddress, valueAsInt);
    return sendMessage();
}

bool KnxTpUart::groupWrite2ByteFloat(byte groupAddress[2], float value) {
    createKNXMessageFrame(2, KNX_COMMAND_WRITE, groupAddress, 0);
    _tg->set2ByteFloatValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupWrite2ByteInt(byte groupAddress[2], int value) {
    createKNXMessageFrame(2, KNX_COMMAND_WRITE, groupAddress, 0);
    _tg->set2ByteFloatValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupWrite1ByteInt(byte groupAddress[2], int value) {
    createKNXMessageFrame(2, KNX_COMMAND_WRITE, groupAddress, 0);
    _tg->set1ByteIntValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupWrite4ByteFloat(byte groupAddress[2], float value) {
    createKNXMessageFrame(2, KNX_COMMAND_WRITE, groupAddress, 0);
    _tg->set4ByteFloatValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupWrite14ByteText(byte groupAddress[2], String value) {
    createKNXMessageFrame(2, KNX_COMMAND_WRITE, groupAddress, 0);
    _tg->set14ByteValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupAnswerBool(byte groupAddress[2], bool value) {
    int valueAsInt = 0;
    if (value) {
        valueAsInt = B00000001;
    }
    
    createKNXMessageFrame(2, KNX_COMMAND_ANSWER, groupAddress, valueAsInt);
    return sendMessage();
}

bool KnxTpUart::groupAnswer1ByteInt(byte groupAddress[2], int value) {
    createKNXMessageFrame(2, KNX_COMMAND_ANSWER, groupAddress, 0);
    _tg->set1ByteIntValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupAnswer2ByteFloat(byte groupAddress[2], float value) {
    createKNXMessageFrame(2, KNX_COMMAND_ANSWER, groupAddress, 0);
    _tg->set2ByteFloatValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupAnswer2ByteInt(byte groupAddress[2], int value) {
    createKNXMessageFrame(2, KNX_COMMAND_ANSWER, groupAddress, 0);
    _tg->set2ByteFloatValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupAnswer4ByteFloat(byte groupAddress[2], float value) {
    createKNXMessageFrame(2, KNX_COMMAND_ANSWER, groupAddress, 0);
    _tg->set4ByteFloatValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupAnswer14ByteText(byte groupAddress[2], String value) {
    createKNXMessageFrame(2, KNX_COMMAND_ANSWER, groupAddress, 0);
    _tg->set14ByteValue(value);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::groupWriteTime(byte groupAddress[2], int day, int hours, int minutes, int seconds) {
    createKNXMessageFrame(2, KNX_COMMAND_WRITE, groupAddress, 0);
    _tg->setKNXTime(day, hours, minutes, seconds);
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::individualAnswerAddress() {
    createKNXMessageFrame(2, KNX_COMMAND_INDIVIDUAL_ADDR_RESPONSE, PA_INTEGER(0,0,0), 0);
    _tg->createChecksum();
    return sendMessage();    
}

bool KnxTpUart::individualAnswerMaskVersion(int area, int line, int member) {
    createKNXMessageFrameIndividual(4, KNX_COMMAND_MASK_VERSION_RESPONSE, PA_INTEGER(area, line, member), 0);
    _tg->setCommunicationType(KNX_COMM_NDP);
    _tg->setBufferByte(8, 0x07); // Mask version part 1 for BIM M 112
    _tg->setBufferByte(9, 0x01); // Mask version part 2 for BIM M 112
    _tg->createChecksum();
    return sendMessage();
}

bool KnxTpUart::individualAnswerAuth(int accessLevel, int sequenceNo, int area, int line, int member) {
    createKNXMessageFrameIndividual(3, KNX_COMMAND_ESCAPE, PA_INTEGER(area, line, member), KNX_EXT_COMMAND_AUTH_RESPONSE);
    _tg->setCommunicationType(KNX_COMM_NDP);
    _tg->setSequenceNumber(sequenceNo);
    _tg->setBufferByte(8, accessLevel);
    _tg->createChecksum();
    return sendMessage();
}

void KnxTpUart::createKNXMessageFrame(int payloadlength, KnxCommandType command, byte groupAddress[2], int firstDataByte) {
    _tg->clear();
    _tg->setSourceAddress(_individualAddress);
    _tg->setTargetGroupAddress(groupAddress);
    _tg->setFirstDataByte(firstDataByte);
    _tg->setCommand(command);
    _tg->setPayloadLength(payloadlength);
    _tg->createChecksum();
}

void KnxTpUart::createKNXMessageFrameIndividual(int payloadlength, KnxCommandType command, byte targetIndividualAddress[2], int firstDataByte) {
    _tg->clear();
    _tg->setSourceAddress(_individualAddress);
    _tg->setTargetIndividualAddress(targetIndividualAddress);
    _tg->setFirstDataByte(firstDataByte);
    _tg->setCommand(command);
    _tg->setPayloadLength(payloadlength);
    _tg->createChecksum();
}

bool KnxTpUart::sendNCDPosConfirm(int sequenceNo, byte targetIndividualAddress[2]) {
    _tg_ptp->clear();
    _tg_ptp->setSourceAddress(_individualAddress);
    _tg_ptp->setTargetIndividualAddress(targetIndividualAddress);
    _tg_ptp->setSequenceNumber(sequenceNo);
    _tg_ptp->setCommunicationType(KNX_COMM_NCD);
    _tg_ptp->setControlData(KNX_CONTROLDATA_POS_CONFIRM);
    _tg_ptp->setPayloadLength(1);
    _tg_ptp->createChecksum();
    
    
    int messageSize = _tg_ptp->getTotalLength();
    
    uint8_t sendbuf[2];
    for (int i = 0; i < messageSize; i++) {
        if (i == (messageSize - 1)) {
            sendbuf[0] = TPUART_DATA_END;
        } else {
            sendbuf[0] = TPUART_DATA_START_CONTINUE;
        }
        
        sendbuf[0] |= i;
        sendbuf[1] = _tg_ptp->getBufferByte(i);
        
        _serialport->write(sendbuf, 2);
    }
    
    
    int confirmation;
    while(true) {
        confirmation = serialRead();
        if (confirmation == B10001011) {
            return true; // Sent successfully
        } else if (confirmation == B00001011) {
            return false;
        } else if (confirmation == -1) {
            // Read timeout
            return false;
        }
    }
    
    return false;
}

bool KnxTpUart::sendMessage() {
    int messageSize = _tg->getTotalLength();

    uint8_t sendbuf[2];
    for (int i = 0; i < messageSize; i++) {
        if (i == (messageSize - 1)) {
            sendbuf[0] = TPUART_DATA_END;
        } else {
            sendbuf[0] = TPUART_DATA_START_CONTINUE;
        }
        
        sendbuf[0] |= i;
        sendbuf[1] = _tg->getBufferByte(i);
        
        _serialport->write(sendbuf, 2);
    }


    int confirmation;
    while(true) {
        confirmation = serialRead();
        if (confirmation == B10001011) {
            delay (SERIAL_WRITE_DELAY_MS);
            return true; // Sent successfully
        } else if (confirmation == B00001011) {
            delay (SERIAL_WRITE_DELAY_MS);
            return false;
        } else if (confirmation == -1) {
            // Read timeout
            delay (SERIAL_WRITE_DELAY_MS);
            return false;
        }
    }

    return false;
}

void KnxTpUart::sendAck() {
    TPUART_DEBUG_PORT.print("Send ACK");
    byte sendByte = B00010001;
    _serialport->write(sendByte);
    delay(SERIAL_WRITE_DELAY_MS);
}

void KnxTpUart::sendNotAddressed() {
    byte sendByte = B00010000;
    _serialport->write(sendByte);
    delay(SERIAL_WRITE_DELAY_MS);
}

int KnxTpUart::serialRead() {
    unsigned long startTime = millis();
#if defined(TPUART_DEBUG)
    TPUART_DEBUG_PORT.print("Available: ");
    TPUART_DEBUG_PORT.println(_serialport->available());
#endif
    
    while (! (_serialport->available() > 0)) {
        if (abs(millis() - startTime) > SERIAL_READ_TIMEOUT_MS) {
            // Timeout
#if defined(TPUART_DEBUG)
            TPUART_DEBUG_PORT.println("Timeout while receiving message");
#endif
            return -1;
        }
        delay(1);
    }
    
    int inByte = _serialport->read();
    checkErrors();
    printByte(inByte);
    
    return inByte;
}

void KnxTpUart::addListenGroupAddress(byte address[]) {
    if (_listen_group_address_count >= MAX_LISTEN_GROUP_ADDRESSES) {
#if defined(TPUART_DEBUG)
        TPUART_DEBUG_PORT.println("Already listening to MAX_LISTEN_GROUP_ADDRESSES, cannot listen to another");
#endif
        return;
    }
    
    _listen_group_addresses[_listen_group_address_count][0]=address[0];
    _listen_group_addresses[_listen_group_address_count][1]=address[1];
    _listen_group_address_count++;

/*
#if defined(TPUART_DEBUG)

    for (int i = 0; i < _listen_group_address_count; i++) {

        TPUART_DEBUG_PORT.print("Listen for: [");
        TPUART_DEBUG_PORT.print(i);
        TPUART_DEBUG_PORT.print("] -> ");
        TPUART_DEBUG_PORT.print(_listen_group_addresses[i][0]);
        TPUART_DEBUG_PORT.print("/");
        TPUART_DEBUG_PORT.print(_listen_group_addresses[i][1]);
        TPUART_DEBUG_PORT.print("/");
        TPUART_DEBUG_PORT.print(_listen_group_addresses[i][2]);
        TPUART_DEBUG_PORT.println("");
    }

#endif
*/

}

bool KnxTpUart::isListeningToGroupAddress(byte address[2]) {
    

    for (int i = 0; i < _listen_group_address_count; i++) {
    
        if ( (_listen_group_addresses[i][0] == address[0])
                && (_listen_group_addresses[i][1] == address[1])) {
            return true;
        }
    }

    return false;
}
