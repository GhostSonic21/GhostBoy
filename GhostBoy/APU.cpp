#include "APU.h"
#include <stdio.h>


APU::APU()
{
	// Set up SDL audio spec
	SDL_AudioSpec audioSpec;
	//audioSpec.freq = 44100;
	audioSpec.freq = 48000;
	audioSpec.format = AUDIO_F32SYS;
	audioSpec.channels = 2;
	audioSpec.samples = samplesize;	// Adjust as needed
	audioSpec.callback = NULL;
	//audioSpec.userdata = this;

	SDL_AudioSpec obtainedSpec;
	//SDL_OpenAudio(&audioSpec, &obtainedSpec);
	//deviceID = 1;
	deviceID = SDL_OpenAudioDevice(NULL, 0, &audioSpec, &obtainedSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (deviceID == 0) {
		cout << SDL_GetError() << "\n";
	}
	SDL_PauseAudioDevice(deviceID, 0);
	//AudioHandler::setupAudio();
}


APU::~APU()
{
}

void APU::sendData(uint16_t address, uint8_t data)
{
	// TODO: Writes can't happen when powered off
	// Wave table can't be read/written at certain times
	uint8_t apuRegister = address & 0xFF;
	// Pulse 1 redirect
	if (apuRegister >= 0x10 && apuRegister <= 0x14) {
		squareOne.writeRegister(apuRegister, data);
	}
	// Pulse 2 redirect
	// ignore 0x15 since sweep doesn't exist
	else if (apuRegister >= 0x16 && apuRegister <= 0x19) {
		squareTwo.writeRegister(apuRegister, data);
	}
	else if (apuRegister >= 0x1A && apuRegister <= 0x1E) {
		waveChannel.writeRegister(address, data);
	}
	else if (apuRegister >= 0x1F && apuRegister <= 0x23) {
		noiseChannel.writeRegister(address, data);
	}
	else if (apuRegister >= 0x24 && apuRegister <= 0x26) {
		switch (apuRegister) {
			case 0x24:
				// Vin bits don't do anything right now. It has something to do with cartridge mixing.
				// Right
				rightVol = data & 0x7;
				vinRightEnable = (data & 0x8) == 0x8;
				// left
				leftVol = (data >> 4) & 0x7;
				vinLeftEnable = (data & 0x80) == 0x80;
				break;
			case 0x25:
				// Adjusts the enables on left and right
				for (int i = 0; i < 4; i++) {
					rightEnables[i] = ((data >> i) & 0x1) == 0x1;
				}
				for (int i = 0; i < 4; i++) {
					leftEnables[i] = ((data >> (i+4)) & 0x1) == 0x1;
				}
				break;
			case 0x26:
				// I don't think writing to length statues does anything
				// Power control
				
				// Shut off event loop
				// Writes 0 to every register besides this one
				if ((data & 0x80) != 0x80) {
					for (int i = 0xFF10; i <= 0xFF25; i++) {
						sendData(i, 0);
					}
					powerControl = false;
				}
				// Only turn on if powerControl was previously off
				else if (!powerControl) {
					// Turn on event resets channels, probably do that later.
					frameSequencer = 0;
					// Reset wave table
					for (int i = 0; i < 16; i++) {
						waveChannel.writeRegister(0xFF30 | i, 0);
					}
					powerControl = true;
				}
				break;
		}
	}
	else if (apuRegister >= 0x30 && apuRegister <= 0x3F) {
		waveChannel.writeRegister(address, data);
	}
}

uint8_t APU::recieveData(uint16_t address)
{
	uint8_t returnData = 0;
	uint8_t apuRegister = address & 0xFF;
	if (apuRegister >= 0x10 && apuRegister <= 0x14) {
		returnData = squareOne.readRegister(apuRegister);
	}
	else if (apuRegister >= 0x16 && apuRegister <= 0x19) {
		returnData = squareTwo.readRegister(apuRegister);
	}
	else if (apuRegister >= 0x1A && apuRegister <= 0x1E) {
		returnData = waveChannel.readRegister(address);
	}
	else if (apuRegister >= 0x1F && apuRegister <= 0x23) {
		returnData = noiseChannel.readRegister(address);
	}
	else if (apuRegister >= 0x24 && apuRegister <= 0x26) {
		switch (apuRegister) {
			case 0x24:
				returnData = (rightVol) | (vinRightEnable << 3) | (leftVol << 4) | (vinLeftEnable << 7);
				break;
			case 0x25:
				// Adjusts the enables on left and right
				for (int i = 0; i < 4; i++) {
					returnData |= (rightEnables[i] << i);
				}
				for (int i = 0; i < 4; i++) {
					returnData |= (leftEnables[i] << (i+4));
				}
				break;
			case 0x26:
				// Power Control
				returnData |= powerControl << 7;
				returnData |= squareOne.getRunning() << 0;
				returnData |= squareTwo.getRunning() << 1;
				returnData |= waveChannel.getRunning() << 2;
				returnData |= noiseChannel.getRunning() << 3;
				break;
		}
	}
	else if (apuRegister >= 0x30 && apuRegister <= 0x3F) {
		returnData = waveChannel.readRegister(address);
	}
	if (apuRegister <= 0x26) {
		returnData |= readOrValues[apuRegister - 0x10];
	}
	return returnData;
}

void APU::step(int cycles)
{
	// 1 CPU Cycle = 1 APU cycle
	while (cycles-- != 0) {
		// Frame Sequencer
		// Ticks every 8192 CPU cycles (on a 512 scale, 4194304/512 = 8192).
		if (--frameSequenceCountDown <= 0) {
			frameSequenceCountDown = 8192;
			switch (frameSequencer) {
				case 0:
					squareOne.lengthClck();
					squareTwo.lengthClck();
					waveChannel.lengthClck();
					noiseChannel.lengthClck();
					break;
				case 1:
					break;
				case 2:
					squareOne.sweepClck();
					squareOne.lengthClck();
					squareTwo.lengthClck();
					waveChannel.lengthClck();
					noiseChannel.lengthClck();
					break;
				case 3:
					break;
				case 4:
					squareOne.lengthClck();
					squareTwo.lengthClck();
					waveChannel.lengthClck();
					noiseChannel.lengthClck();
					break;
				case 5:
					break;
				case 6:
					squareOne.sweepClck();
					squareOne.lengthClck();
					squareTwo.lengthClck();
					waveChannel.lengthClck();
					noiseChannel.lengthClck();
					break;
				case 7:
					squareOne.envClck();
					squareTwo.envClck();
					noiseChannel.envClck();
					break;
			}
			frameSequencer++;
			if (frameSequencer >= 8) {
				frameSequencer = 0;
			}
		}

		// Step all the channels
		squareOne.step();
		squareTwo.step();
		waveChannel.step();
		noiseChannel.step();

		if (--downSampleCount <= 0) {
			downSampleCount = 87;

			// Left
			float bufferin0 = 0;
			float bufferin1 = 0;
			int volume = (128*leftVol)/7;	// Should approximate an integer for the mixer volume
			if (leftEnables[0]) {
				bufferin1 = ((float)squareOne.getOutputVol()) / 100;
				SDL_MixAudioFormat((Uint8*)&bufferin0, (Uint8*)&bufferin1, AUDIO_F32SYS, sizeof(float), volume);
			}
			if (leftEnables[1]) {
				bufferin1 = ((float)squareTwo.getOutputVol()) / 100;
				SDL_MixAudioFormat((Uint8*)&bufferin0, (Uint8*)&bufferin1, AUDIO_F32SYS, sizeof(float), volume);
			}
			if (leftEnables[2]) {
				bufferin1 = ((float)waveChannel.getOutputVol()) / 100;
				SDL_MixAudioFormat((Uint8*)&bufferin0, (Uint8*)&bufferin1, AUDIO_F32SYS, sizeof(float), volume);
			}
			if (leftEnables[3]) {
				bufferin1 = ((float)noiseChannel.getOutputVol()) / 100;
				SDL_MixAudioFormat((Uint8*)&bufferin0, (Uint8*)&bufferin1, AUDIO_F32SYS, sizeof(float), volume);
			}
			mainBuffer[bufferFillAmount] = bufferin0;

			// Right
			bufferin0 = 0;
			volume = (128 * rightVol) / 7;
			if (rightEnables[0]) {
				bufferin1 = ((float)squareOne.getOutputVol()) / 100;
				SDL_MixAudioFormat((Uint8*)&bufferin0, (Uint8*)&bufferin1, AUDIO_F32SYS, sizeof(float), volume);
			}
			if (rightEnables[1]) {
				bufferin1 = ((float)squareTwo.getOutputVol()) / 100;
				SDL_MixAudioFormat((Uint8*)&bufferin0, (Uint8*)&bufferin1, AUDIO_F32SYS, sizeof(float), volume);
			}
			if (rightEnables[2]) {
				bufferin1 = ((float)waveChannel.getOutputVol()) / 100;
				SDL_MixAudioFormat((Uint8*)&bufferin0, (Uint8*)&bufferin1, AUDIO_F32SYS, sizeof(float), volume);
			}
			if (rightEnables[3]) {
				bufferin1 = ((float)noiseChannel.getOutputVol()) / 100;
				SDL_MixAudioFormat((Uint8*)&bufferin0, (Uint8*)&bufferin1, AUDIO_F32SYS, sizeof(float), volume);
			}

			//AudioHandler::playAudio();
			// Section below off to play audio
			mainBuffer[bufferFillAmount + 1] = bufferin0;

			bufferFillAmount += 2;
		}
		if (bufferFillAmount >= samplesize) {
			bufferFillAmount = 0;
			// Delay execution and the let queue drain to about a frame's worth
			while ((SDL_GetQueuedAudioSize(deviceID)) > samplesize * sizeof(float)) {
				SDL_Delay(1);
			}
			SDL_QueueAudio(deviceID, mainBuffer, samplesize*sizeof(float));
		}
	}
}

SDL_AudioDeviceID APU::getDeviceId()
{
	return deviceID;
}
