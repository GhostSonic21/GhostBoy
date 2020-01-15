#include "NoiseChannel.h"



NoiseChannel::NoiseChannel()
{
}


NoiseChannel::~NoiseChannel()
{
}

uint8_t NoiseChannel::readRegister(uint16_t address)
{
	// TODO
	uint8_t returnData = 0;

	switch (address) {
	case 0xFF1F:
		// Not used
		break;
	case 0xFF20:
		returnData = lengthLoad & 0x3F;
		break;
	case 0xFF21:
		returnData = (envelopePeriodLoad & 0x7) | (envelopeAddMode << 3) | ((volumeLoad & 0xF) << 4);
		break;
	case 0xFF22:
		returnData = (divisorCode) | (widthMode << 3) | (clockShift << 4);
		break;
	case 0xFF23:
		returnData = (lengthEnable << 6) | (triggerBit << 7);	// Trigger bit probably?
		break;

	}

	return returnData;
}

void NoiseChannel::writeRegister(uint16_t address, uint8_t data)
{
	switch (address) {
		case 0xFF1F:
			// Not used
			break;
		case 0xFF20:
			lengthLoad = data & 0x3F;
			lengthCounter = 64 - lengthLoad;
			break;
		case 0xFF21:
			// See if dac is enabled, if all high 5 bits are not 0
			dacEnabled = (data & 0xF8) != 0;
			// Starting Volume
			volumeLoad = (data >> 4) & 0xF;
			// Add mode
			envelopeAddMode = (data & 0x8) == 0x8;
			// Period
			envelopePeriodLoad = (data & 0x7);
			envelopePeriod = envelopePeriodLoad;
			// TEMP?
			volume = volumeLoad;
			break;
		case 0xFF22:
			divisorCode = data & 0x7;
			widthMode = (data & 0x8) == 0x8;
			clockShift = (data >> 4) & 0xF;
			break;
		case 0xFF23:
			lengthEnable = (data & 0x40) == 0x040;
			triggerBit = (data & 0x80) == 0x80;
			if (triggerBit) {
				trigger();
			}
			break;

	}
}

void NoiseChannel::step()
{
	if (--timer <= 0) {
		timer = divisors[divisorCode] << clockShift;	// odd

		//It has a 15 - bit shift register with feedback.When clocked by the frequency timer, the low two bits(0 and 1) are XORed, 
		//all bits are shifted right by one, and the result of the XOR is put into the now - empty high bit.If width mode is 1 (NR43), 
		//the XOR result is ALSO put into bit 6 AFTER the shift, resulting in a 7 - bit LFSR.
		//The waveform output is bit 0 of the LFSR, INVERTED.
		uint8_t result = (lfsr & 0x1) ^ ((lfsr >> 1) & 0x1);
		lfsr >>= 1;
		lfsr |= result << 14;
		if (widthMode) {
			lfsr &= ~0x40;
			lfsr |= result << 6;
		}
		if (enabled && dacEnabled && (lfsr & 0x1) == 0) {
			outputVol = volume;
		}
		else {
			outputVol = 0;
		}
	}
}

void NoiseChannel::envClck()
{
	// Envelope tick when it's zero
	if (--envelopePeriod <= 0) {
		// Reload period
		// does this loop or?
		envelopePeriod = envelopePeriodLoad;
		if (envelopePeriod == 0) {
			envelopePeriod = 8;		// Some obscure behavior, But I don't understand this behavior at all..
		}
		// Should envelopePeriod > 0 be here?
		if (envelopeRunning && envelopePeriodLoad > 0) {
			if (envelopeAddMode && volume < 15) {
				volume++;
			}
			else if (!envelopeAddMode && volume > 0) {
				volume--;
			}
		}
		if (volume == 0 || volume == 15) {
			envelopeRunning = false;
		}
	}
}

void NoiseChannel::lengthClck()
{
	if (lengthCounter > 0 && lengthEnable) {
		lengthCounter--;
		if (lengthCounter == 0) {
			enabled = false;	// Disable channel
		}
	}
}

uint8_t NoiseChannel::getOutputVol()
{
	return outputVol;
}

bool NoiseChannel::getRunning()
{
	return lengthCounter > 0;
}

void NoiseChannel::trigger()
{
	enabled = true;
	if (lengthCounter == 0) {
		lengthCounter = 64;
	}
	timer = divisors[divisorCode] << clockShift;
	envelopePeriod = envelopePeriodLoad;
	envelopeRunning = true;
	volume = volumeLoad;
	lfsr = 0x7FFF;
}
