#include "MCP3208.h"

MCP3208::MCP3208() {}

void MCP3208::begin(uint8_t cs, SPIClass &spi) {
	this->cs = cs;
	pinMode(cs, OUTPUT);
	digitalWrite(cs, HIGH);
	this->spi = &spi;
	this->spi->begin();
	}

void MCP3208::analogReadResolution(uint8_t bits) {
	if (bits <=12 && bits >= 1)
		this->bits = bits;
	else this->bits = 12;
	}

uint16_t MCP3208::analogRead(uint8_t channel) {
	// MCP3208 max SPI clock: 2MHz at 5V (per datasheet)
	// Using SPI_MODE0: CPOL=0, CPHA=0 (clock idle low, sample on rising edge)
	SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);

	uint8_t addr = 0b01100000 | ((channel & 0b111) << 2);

	spi->beginTransaction(spiSettings);
	digitalWrite(cs, LOW);
	spi->transfer(addr);
	uint8_t byte1 = spi->transfer(0x00);
	uint8_t byte2 = spi->transfer(0x00);
	digitalWrite(cs, HIGH);
	spi->endTransaction();

	return ((byte1 << 4) | (byte2 >> 4)) >> (RESOLUTION_MCP320X - bits);
	}