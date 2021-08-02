#include "SquareChannel.h"
#include <stdio.h>



SquareChannel::SquareChannel()
{
	timerLoad = 0;
	timer = 0;
}


SquareChannel::~SquareChannel()
{

}

uint8_t SquareChannel::readRegister(uint16_t address)
{
	uint8_t returnData = 0;

	uint8_t squareRegister = (address & 0xF) % 0x5;
	switch (squareRegister) {
		// Sweep. Only on Square 1
	case 0x0:
		returnData = (sweepShift) | ((sweepNegate) << 3) | (sweepPeriodLoad << 4);
		break;
		// Duty, Length Load
	case 0x1:
		returnData = (lengthLoad & 0x3F) | ((duty & 0x3) << 6);
		break;
		// Envelope
	case 0x2:
		returnData = (envelopePeriodLoad & 0x7) | (envelopeAddMode << 3) | ((volumeLoad & 0xF) << 4);
		break;
	case 0x3:
		returnData = timerLoad & 0xFF;
		break;
		// Trigger, length enable, frequency MSB
	case 0x4:
		returnData = ((timerLoad >> 8) & 0x7) | (lengthEnable << 6) | (triggerBit << 7);	// Trigger bit probably?
		// Trigger is on 0x80, bit 7.
		break;
	}

	return returnData;
}

void SquareChannel::writeRegister(uint16_t address, uint8_t data)
{
	uint8_t squareRegister = (address & 0xF) % 0x5;
	switch (squareRegister) {
		// Sweep. Only on Square 1
		case 0x0:
			sweepShift = data & 0x7;
			sweepNegate = (data & 0x8) == 0x8;
			sweepPeriodLoad = (data >> 4) & 0x7;
			//sweepPeriod = sweepPeriodLoad;
			break;
		// Duty, Length Load
		case 0x1:
			lengthLoad = data & 0x3F;
			lengthCounter = 64 - (lengthLoad & 0x3F);
			duty = (data >> 6) & 0x3;
			break;
		// Envelope
		case 0x2:
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
		case 0x3:
		// Frequency LSB
			timerLoad = (timerLoad & 0x700) | data;
			break;
		// Trigger, length enable, frequency MSB
		case 0x4:
			timerLoad = (timerLoad & 0xFF) | ((data & 0x7) << 8);
			lengthEnable = (data & 0x40) == 0x40;
			// This should happen LAST, it'll cause issues if it isn't last
			triggerBit = (data & 0x80) == 0x80;
			if ((data & 0x80) == 0x80) {
				trigger();
			}
			// Trigger is on 0x80, bit 7.
			break;
	}

}

void SquareChannel::step()
{
	if (--timer <= 0) {
		//timer = timerLoad;
		timer = (2048 - timerLoad) * 4;	// ???
		sequencePointer = (sequencePointer + 1) & 0x7;
	}

	if (enabled && dacEnabled) {
		outputVol = volume;
	}
	else {
		outputVol = 0;
	}

	if (!dutyTable[duty][sequencePointer]) {
		outputVol = 0;
	}
}

uint8_t SquareChannel::getOutputVol()
{
	return outputVol;
}

void SquareChannel::lengthClck()
{
	if (lengthCounter > 0 && lengthEnable) {
		lengthCounter--;
		if (lengthCounter == 0) {
			enabled = false;	// Disable channel
		}
	}
}

void SquareChannel::envClck()
{
	// Envelope tick when it's zero
	if(--envelopePeriod <= 0){
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

void SquareChannel::sweepClck()
{
	// Frequency calculation consists of taking the value in the frequency shadow register, shifting it right by sweep shift, 
	// optionally negating the value, and summing this with the frequency shadow register to produce a new frequency.
	// What is done with this new frequency depends on the context.
	// The overflow check simply calculates the new frequency and if this is greater than 2047, square 1 is disabled. 

	// The sweep timer is clocked at 128 Hz by the frame sequencer. 
	// When it generates a clock and the sweep's internal enabled flag is set and the sweep period is not zero, a new frequency is calculated and the overflow check is performed. 
	// If the new frequency is 2047 or less and the sweep shift is not zero, this new frequency is written back to the shadow frequency and square 1's frequency in NR13 and NR14, 
	// then frequency calculation and overflow check are run AGAIN immediately using this new value, but this second new frequency is not written back.

	// Square 1's frequency can be modified via NR13 and NR14 while sweep is active, but the shadow frequency won't be affected so the next time the sweep updates the channel's frequency this modification will be lost. 
	if (--sweepPeriod <= 0) {
		sweepPeriod = sweepPeriodLoad;
		if (sweepPeriod == 0) {
			sweepPeriod = 8;
		}
		if (sweepEnable && sweepPeriodLoad > 0) {
			uint16_t newFreq = sweepCalculation();
			if (newFreq <= 2047 && sweepShift > 0) {
				sweepShadow = newFreq;
				timerLoad = newFreq;
				sweepCalculation();
			}
			sweepCalculation();
		}
	}
}

bool SquareChannel::getRunning()
{
	return lengthCounter > 0;
}

uint16_t SquareChannel::sweepCalculation()
{
	uint16_t newFreq = 0;
	newFreq = sweepShadow >> sweepShift;
	if (sweepNegate) {
		newFreq = sweepShadow - newFreq;
	}
	else {
		newFreq = sweepShadow + newFreq;
	}
	// Should I assume that there's some sort of underflow with this frequency calculation/overflow check? 
	// It'd disable the channel in the event of an underflow at least. That might be intended
	if (newFreq > 2047) {
		enabled = false;
	}

	return newFreq;
}

void SquareChannel::trigger()
{
	// TODO: More weird behavior relating to the trigger
	enabled = true;
	if (lengthCounter == 0) {
		lengthCounter = 64;	// It's a little large
	}
	timer = (2048 - timerLoad) * 4;
	envelopeRunning = true;
	envelopePeriod = envelopePeriodLoad;
	volume = volumeLoad;
	// Sweep trigger stuff
	sweepShadow = timerLoad;
	sweepPeriod = sweepPeriodLoad;
	if (sweepPeriod == 0) {
		sweepPeriod = 8;
	}
	sweepEnable = sweepPeriod > 0 || sweepShift > 0;
	// If the sweep shift is non-zero, frequency calculation and the overflow check are performed immediately.
	if (sweepShift > 0) {
		// Overflow check?
		sweepCalculation();
	}
}
