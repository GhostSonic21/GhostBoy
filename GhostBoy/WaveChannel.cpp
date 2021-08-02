#include "WaveChannel.h"



WaveChannel::WaveChannel()
{
	timerLoad = 0;
	timer = 0;
}


WaveChannel::~WaveChannel()
{
}

uint8_t WaveChannel::readRegister(uint16_t address)
{
	// Eh
	uint8_t returnData = 0;

	uint8_t registerVal = address & 0xF;
	if (address >= 0xFF1A && address <= 0xFF1E) {
		switch (registerVal) {
		case 0xA:
			returnData = (dacEnabled) << 7;
			break;
		case 0xB:
			returnData = lengthLoad;
			break;
		case 0xC:
			returnData = volumeCode << 5;
			break;
		case 0xD:
			returnData = timerLoad & 0xFF;
			break;
		case 0xE:
			returnData = ((timerLoad >> 8) & 0x7) | (lengthEnable << 6) | (triggerBit << 7);	// Trigger bit probably?
			break;
		}
	}
	// wave ram
	else if (address >= 0xFF30 && address <= 0xFF3F) {
		returnData = waveTable[registerVal];
	}

	return returnData;
}

void WaveChannel::writeRegister(uint16_t address, uint8_t data)
{
	uint8_t registerVal = address & 0xF;
	if(address >= 0xFF1A && address <= 0xFF1E){
		switch (registerVal) {
			case 0xA:
				dacEnabled = (data & 0x80) == 0x80;
				break;
			case 0xB:
				lengthLoad = data;
				lengthCounter = 256 - lengthLoad;
				break;
			case 0xC:
				volumeCode = (data >> 5) & 0x3;
				break;
			case 0xD:
				timerLoad = (timerLoad & 0x700) | data;
				break;
			case 0xE:
				timerLoad = (timerLoad & 0xFF) | ((data & 0x7) << 8);
				lengthEnable = (data & 0x40) == 0x40;
				triggerBit = (data & 0x80) == 0x80;
				if (triggerBit) {
					trigger();	// Trigger event
				}
				break;
		}
	}
	// wave ram
	else if (address >= 0xFF30 && address <= 0xFF3F) {
		waveTable[registerVal] = data;
	}
}

void WaveChannel::step()
{
	if (--timer <= 0) {
		timer = (2048 - timerLoad) * 2;
		// Should increment happen before or after?
		positionCounter = (positionCounter + 1) & 0x1F;
		// Decide output volume
		if (enabled && dacEnabled) {
			// Decide what byte it should be first
			int position = positionCounter / 2;
			uint8_t outputByte = waveTable[position];
			bool highBit = (positionCounter & 0x1) == 0;
			if (highBit) {
				outputByte >>= 4;
			}
			outputByte &= 0xF;
			// Handle volume code. 0 shouldn't occur.
			if (volumeCode > 0) {
				outputByte >>= volumeCode - 1;
			}
			else {
				outputByte = 0;
			}
			outputVol = outputByte;
		}
		else {
			outputVol = 0;
		}
	}
}

void WaveChannel::lengthClck()
{
	if (lengthCounter > 0 && lengthEnable) {
		lengthCounter--;
		if (lengthCounter == 0) {
			enabled = false;	// Disable channel
		}
	}
}

uint8_t WaveChannel::getOutputVol()
{
	return outputVol;
}

bool WaveChannel::getRunning()
{
	return lengthCounter > 0;
}

void WaveChannel::trigger()
{
	enabled = true;
	if (lengthCounter == 0) {
		lengthCounter = 256;
	}
	timer = (2048 - timerLoad) * 2;
	positionCounter = 0;
}
