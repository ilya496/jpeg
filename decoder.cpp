#include <iostream>
#include <fstream>

#include "jpg.h"

// APPNs simply get skipped based on length
void readAPPN(std::ifstream& file, Header* const header) {
	std::cout << "Reading APPN Marker\n";
	uint32_t length = (file.get() << 8) + file.get();

	for (uint32_t i = 0; i < length - 2; ++i) {
		file.get();
	}
}

// comments simply get skipped based on length
void readComment(std::ifstream& file, Header* const header) {
	std::cout << "Reading COM Marker\n";
	uint32_t length = (file.get() << 8) + file.get();

	for (uint32_t i = 0; i < length - 2; ++i) {
		file.get();
	}
}


void readQuantizationTable(std::ifstream& file, Header* const header) {
	std::cout << "Reading DQT Marker\n";
	int length = (file.get() << 8) + file.get();
	length -= 2;

	while (length > 0) {
		byte tableInfo = file.get();
		length -= 1;
		byte tableID = tableInfo & 0x0F;

		if (tableID > 3) {
			std::cout << "Error - Invalid quantization table ID: " << (uint32_t)tableID << '\n';
			header->isValid = false;
			return;
		}

		header->quantizationTables[tableID].set = true;

		if (tableInfo >> 4 != 0) {
			for (uint32_t i = 0; i < 64; ++i) {
				header->quantizationTables[tableID].table[zigZagMap[i]] = (file.get() << 8) + file.get();
			}
			length -= 128;
		} else {
			for (uint32_t i = 0; i < 64; ++i) {
				header->quantizationTables[tableID].table[zigZagMap[i]] = file.get();
			}
			length -= 64;
		}
	}

	if (length != 0) {
		std::cout << "Error - DQT invalid\n";
		header->isValid = false;
	}
}

// SOF specifies frame type, dimensions, and number of color components
void readStartOfFrame(std::ifstream& file, Header* const header) {
	std::cout << "Reading Start of Frame\n";
	if (header->numComponents != 0) {
		std::cout << "Error - Multiple SOFs detected\n";
		header->isValid = false;
		return;
	}

	uint32_t length = (file.get() << 8) + file.get();

	byte precision = file.get();
	if (precision != 8) {
		std::cout << "Error - Invalid precision: " << (uint32_t)precision << '\n';
		header->isValid = false;
		return;
	}

	header->height = (file.get() << 8) + file.get();
	header->width = (file.get() << 8) + file.get();
	if (header->height == 0 || header->width == 0) {
		std::cout << "Error - Invalid dimensions\n";
		header->isValid = false;
		return;
	}

	header->mcuHeight = (header->height + 7) / 8;
	header->mcuWidth = (header->width + 7) / 8;
	header->mcuHeightReal = header->mcuHeight;
	header->mcuWidthReal = header->mcuWidth;

	header->numComponents = file.get();
	if (header->numComponents == 4) {
		std::cout << "Error - CMYK color mode not supported\n";
		header->isValid = false;
		return;
	}

	if (header->numComponents == 0) {
		std::cout << "Error - Number of color components must not be zero\n";
		header->isValid = false;
		return;
	}

	for (uint32_t i = 0; i < header->numComponents; ++i) {
		byte componentID = file.get();
		// component IDs are usually 1, 2, 3 but rarely can be seen as 0, 1, 2
		// always force them into 1, 2, 3 for consistency
		if (componentID == 0) {
			header->zeroBased = true;
		}

		if (header->zeroBased) {
			componentID += 1;
		}

		if (componentID == 4 || componentID == 5) {
			std::cout << "Error - YIQ color mode not supported\n";
			header->isValid = false;
			return;
		}

		if (componentID == 0 || componentID > 3) {
			std::cout << "Error - Invalid component ID: "<< (uint32_t)componentID << '\n';
			header->isValid = false;
			return;
		}

		ColorComponent* component = &header->colorComponents[componentID - 1];
		if (component->used) {
			std::cout << "Error - Duplicate color component ID\n";
			header->isValid = false;
			return;
		}
		component->used = true;

		byte samplingFactor = file.get();
		component->horizontalSamplingFactor = samplingFactor >> 4;
		component->verticalSamplingFactor = samplingFactor & 0x0F;

		if (componentID == 1) {
			if ((component->horizontalSamplingFactor != 1 && component->horizontalSamplingFactor != 2) ||
				(component->verticalSamplingFactor != 1 && component->verticalSamplingFactor != 2)) {
				std::cout << "Error - Sampling factors not supported\n";
				header->isValid = false;
				return;
			}

			if (component->horizontalSamplingFactor == 2 && header->mcuWidth % 2 == 1) {
				header->mcuWidthReal += 1;
			}
			if (component->verticalSamplingFactor == 2 && header->mcuHeight % 2 == 1) {
				header->mcuHeightReal += 1;
			}

			header->horizontalSamplingFactor = component->horizontalSamplingFactor;
			header->verticalSamplingFactor = component->verticalSamplingFactor;
		} else {
			if (component->horizontalSamplingFactor != 1 || component->verticalSamplingFactor != 1) {
				std::cout << "Error - Sampling factors not supported\n";
				header->isValid = false;
				return;
			}
		}

		component->quantizationTableID = file.get();
		if (component->quantizationTableID > 3) {
			std::cout << "Error - Invalid quantization table ID in frame components\n";
			header->isValid = false;
			return;
		}
	}

	if (length - 8 - (3 * header->numComponents) != 0) {
		std::cout << "Error - Invalid SOF\n";
		header->isValid = false;
	}
}

void readRestartInterval(std::ifstream& file, Header* const header) {
	std::cout << "Reading DRI Marker\n";
	uint32_t length = (file.get() << 8) + file.get();

	header->restartInterval = (file.get() << 8) + file.get();
	if (length - 4 != 0) {
		std::cout << "Error - Invalid DRI\n";
		header->isValid = false;
	}
}

void readHuffmanTable(std::ifstream& file, Header* const header) {
	std::cout << "Reading DHT Marker\n";
	int length = (file.get() << 8) + file.get();
	length -= 2;

	while (length > 0) {
		byte tableInfo = file.get();
		byte tableID = tableInfo & 0x0F;
		bool isACTable = tableInfo >> 4;

		if (tableID > 3) {
			std::cout << "Error - Invalid Huffman table ID: " << (uint32_t)tableID << '\n';
			header->isValid = false;
			return;
		}

		HuffmanTable* hTable;

		if (isACTable) {
			hTable = &header->huffmanACTables[tableID];
		} else {
			hTable = &header->huffmanDCTables[tableID];
		}

		hTable->set = true;

		hTable->offsets[0] = 0;
		uint32_t allSymbols = 0;
		for (uint32_t i = 1; i <= 16; ++i) {
			allSymbols += file.get();
			hTable->offsets[i] = allSymbols;
		}

		if (allSymbols > 162) {
			std::cout << "Error - Too many symbols in Huffman table\n";
			header->isValid = false;
			return;
		}

		for (uint32_t i = 0; i < allSymbols; ++i) {
			hTable->symbols[i] = file.get();
		}

		length -= 17 + allSymbols;
	}

	if (length != 0) {
		std::cout << "Error - DHT Invalid\n";
		header->isValid = false;
	}
}

void readStartOfSelection(std::ifstream& file, Header* const header) {
	std::cout << "Reading SOS Marker\n";
	if (header->numComponents == 0) {
		std::cout << "Error - SOS detected before SOF\n";
		header->isValid = false;
		return;
	}

	uint32_t length = (file.get() << 8) + file.get();

	for (uint32_t i = 0; i < header->numComponents; ++i) {
		header->colorComponents[i].used = false;
	}

	byte numComponents = file.get();
	for (uint32_t i = 0; i < numComponents; ++i) {
		byte componentID = file.get();
		// components IDs are usually 1, 2, 3 but rarely can be seen as 0, 1, 2
		if (header->zeroBased) {
			componentID += 1;
		}

		if (componentID > header->numComponents) {
			std::cout << "Error - Invalid color component ID: " << (uint32_t)componentID << '\n';
			header->isValid = false;
			return;
		}

		ColorComponent* component = &header->colorComponents[componentID - 1];
		if (component->used) {
			std::cout << "Error - Duplicate color component ID: " << (uint32_t)componentID << '\n';
			header->isValid = false;
			return;
		}
		component->used = true;

		byte huffmanTableIDs = file.get();
		component->huffmanDCTableID = huffmanTableIDs >> 4;
		component->huffmanACTableID = huffmanTableIDs & 0x0F;

		if (component->huffmanDCTableID > 3) {
			std::cout << "Error - Invalid Huffman DC table ID: " << (uint32_t)component->huffmanDCTableID << '\n';
			header->isValid = false;
			return;
		}

		if (component->huffmanACTableID > 3) {
			std::cout << "Error - Invalid Huffman AC table ID: " << (uint32_t)component->huffmanACTableID << '\n';
			header->isValid = false;
			return;
		}
	}

	header->startOfSelection = file.get();
	header->endOfSelection = file.get();
	byte successiveApproximation = file.get();
	header->successiveApproximationHigh = successiveApproximation >> 4;
	header->successiveApproximationLow = successiveApproximation & 0x0F;

	// Baseline JPGs don't use spectral selection or successive approximation
	if (header->startOfSelection != 0 || header->endOfSelection != 63) {
		std::cout << "Error - Invalid spectral selection\n";
		header->isValid = false;
		return;
	}

	if (header->successiveApproximationHigh != 0 || header->successiveApproximationLow != 0) {
		std::cout << "Error - Invalid successive approximation\n";
		header->isValid = false;
		return;
	}

	if (length - 6 - (2 * numComponents) != 0) {
		std::cout << "Error - Invalid SOS\n";
		header->isValid = false;
	}
}

Header* readJPG(const std::string& filename) {
	// open file
	std::ifstream file(filename, std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		std::cout << "Error - Error opening input file\n";
		return nullptr;
	}

	Header* header = new (std::nothrow) Header();
	if (header == nullptr) {
		std::cout << "Error - Memory error\n";
		file.close();
		return nullptr;
	}

	byte last = file.get();
	byte current = file.get();
	if (last != 0xFF || current != SOI) {
		header->isValid = false;
		file.close();
		std::cout << "Error - File does not start with SOI marker\n";
		return header;
	}

	last = file.get();
	current = file.get();

	while (header->isValid) {
		if (!file) {
			std::cout << "Error - File ended prematurely\n";
			header->isValid = false;
			file.close();
			return header;
		}

		if (last != 0xFF) {
			std::cout << "Error - Expected a marker\n";
			header->isValid = false;
			file.close();
			return header;
		}

		if (current == SOF0) {
			header->frameType = SOF0;
			readStartOfFrame(file, header);
		} else if (current == DQT) {
			readQuantizationTable(file, header);
		} else if (current == DHT) {
			readHuffmanTable(file, header);
		} else if (current == SOS) {
			readStartOfSelection(file, header);
			// break from while loop after SOS
			break;
		} else if (current == DRI) {
			readRestartInterval(file, header);
		} else if (current >= APP0 && current <= APP15) {
			readAPPN(file, header);
		} else if (current == COM) {
			readComment(file, header);
		} else if ((current >= JPG0 && current <= JPG13) || 
				current == DNL ||
				current == DHP ||
				current == EXP) {
			readComment(file, header);
		} else if (current == TEM) {
			// TEM has no size
		} else if (current == 0xFF) {
			// any number of 0xFF in a row is allowed and should be ignored
			current = file.get();
			continue;
		} else if (current == SOI) {
			std::cout << "Error - Embedded JPGs not supported\n";
			header->isValid = false;
			file.close();
			return header;
		} else if (current == EOI) {
			std::cout << "Error - EOI detected before SOS\n";
			header->isValid = false;
			file.close();
			return header;
		} else if (current == DAC) {
			std::cout << "Error - Arithmetic Coding mode not supported\n";
			header->isValid = false;
			file.close();
			return header;
		} else if (current >= SOF0 && current <= SOF15) {
			std::cout << "Error - SOF marker not supported: 0x" << std::hex << (uint32_t)current << std::dec << '\n';
			header->isValid = false;
			file.close();
			return header;
		} else if (current >= RST0 && current <= RST7) {
			std::cout << "Error - RSTN detected before SOS\n";
			header->isValid = false;
			file.close();
			return header;
		} else {
			std::cout << "Error - Unknown marker: 0x" << std::hex << (uint32_t)current << std::dec << '\n';
			header->isValid = false;
			file.close();
			return header;
		}

		last = file.get();
		current = file.get();
	}

	// after SOS
	if (header->isValid) {
		current = file.get();
		// read compressed image data
		while (true) {
			if (!file) {
				std::cout << "Error - File ended prematurely\n";
				header->isValid = false;
				file.close();
				return header;
			}

			last = current;
			current = file.get();

			// if marker is found
			if (last == 0xFF) {
				// end of image
				if (current == EOI) {
					break;
				} else if (current == 0x00) {
					// 0xFF00 means put a literal 0xFF in image data and ignore 0x00
					header->huffmanData.push_back(last);
					// overwrite 0x00 with next byte
					current = file.get();
				} else if (current >= RST0 && current <= RST7) {
					// restart marker
					// overwrite marker with next byte
					current = file.get();
				} else if (current == 0xFF) {
					// ignore multiple 0xFF's in a row
					// do nothing
					continue;
				} else {
					std::cout << "Error - Invalid marker during compressed data scan: 0x" << std::hex << (uint32_t)current << std::dec << '\n';
					header->isValid = false;
					file.close();
					return header;
				}
			} else {
				header->huffmanData.push_back(last);
			}
		}
	}

	// validate header info
	if (header->numComponents != 1 &&  header->numComponents != 3) {
		std::cout << "Error - " << (uint32_t)header->numComponents << " color components given (1 or 3 required)" << '\n';
		header->isValid = false;
		file.close();
		return header;
	}

	for (uint32_t i = 0; i < header->numComponents; ++i) {
		if (!header->quantizationTables[header->colorComponents[i].quantizationTableID].set) {
			std::cout << "Error - Color component using uninitialized quantization table\n";
			header->isValid = false;
			file.close();
			return header;
		}

		if (!header->huffmanDCTables[header->colorComponents[i].huffmanDCTableID].set) {
			std::cout << "Error - Color component using uninitialized Huffman DC table\n";
			header->isValid = false;
			file.close();
			return header;
		}

		if (!header->huffmanACTables[header->colorComponents[i].huffmanACTableID].set) {
			std::cout << "Error - Color component using uninitialized HUffman AC table\n";
			header->isValid = false;
			file.close();
			return header;
		}
	}

	file.close();
	return header;
}

void printHeader(const Header* const header) {
	if (header == nullptr) return;

	std::cout << "DQT============\n";
	for (uint32_t i = 0; i < 4; ++i) {
		if (header->quantizationTables[i].set) {
			std::cout << "Table ID: " << i << '\n';
			std::cout << "Table Data:";
			for (uint32_t j = 0; j < 64; ++j) {
				if (j % 8 == 0) {
					std::cout << '\n';
				}
				std::cout << header->quantizationTables[i].table[j] << ' ';
			}
			std::cout << '\n';
		}
	}

	std::cout << "SOF============\n";
	std::cout << "Frame Type: 0x" << std::hex << (uint32_t)header->frameType << std::dec << '\n';
	std::cout << "Height: " << header->height << '\n';
	std::cout << "Width: " << header->width << '\n';
	std::cout << "Color Components:\n";
	for (uint32_t i = 0; i < header->numComponents; ++i) {
		std::cout << "Component ID: " << (i + 1) << '\n';
		std::cout << "Horizontal Sampling Factor: " << (uint32_t)header->colorComponents[i].horizontalSamplingFactor << '\n';
		std::cout << "Vertical Sampling Factor: " << (uint32_t)header->colorComponents[i].verticalSamplingFactor << '\n';
		std::cout << "Quantization Table ID: " << (uint32_t)header->colorComponents[i].quantizationTableID << '\n';
	}

	std::cout << "DHT============\n";
	std::cout << "DC Tables:\n";
	for (uint32_t i = 0; i < 4; ++i) {
		if (header->huffmanDCTables[i].set) {
			std::cout << "Table ID: " << i << '\n';
			std::cout << "Symbols\n";
			for (uint32_t j = 0; j < 16; ++j) {
				std::cout << (j + 1) << ": ";
				for (uint32_t k = header->huffmanDCTables[i].offsets[j]; k < header->huffmanDCTables[i].offsets[j + 1]; ++k) {
					std::cout << std::hex << (uint32_t)header->huffmanDCTables[i].symbols[k] << std::dec << ' ';
				}
				std::cout << '\n';
			}
		}
	}
	std::cout << "AC Tables:\n";
	for (uint32_t i = 0; i < 4; ++i) {
		if (header->huffmanACTables[i].set) {
			std::cout << "Table ID: " << i << '\n';
			std::cout << "Symbols\n";
			for (uint32_t j = 0; j < 16; ++j) {
				std::cout << (j + 1) << ": ";
				for (uint32_t k = header->huffmanACTables[i].offsets[j]; k < header->huffmanACTables[i].offsets[j + 1]; ++k) {
					std::cout << std::hex << (uint32_t)header->huffmanACTables[i].symbols[k] << std::dec << ' ';
				}
				std::cout << '\n';
			}
		}
	}

	std::cout << "SOS============\n";
	std::cout << "Start of Selection: " << (uint32_t)header->startOfSelection << '\n';
	std::cout << "End of Selection: " << (uint32_t)header->endOfSelection << '\n';
	std::cout << "Successive Approximation High: " << (uint32_t)header->successiveApproximationHigh << '\n';
	std::cout << "Successive Approximation Low: " << (uint32_t)header->successiveApproximationLow << '\n';
	for (uint32_t i = 0; i < header->numComponents; ++i) {
		std::cout << "Color Component ID: " << (i + 1) << '\n';
		std::cout << "Huffman DC Table ID: " << (uint32_t)header->colorComponents[i].huffmanDCTableID << '\n';
		std::cout << "Huffman AC Table ID: " << (uint32_t)header->colorComponents[i].huffmanACTableID << '\n';
	}
	std::cout << "Total length of Huffman data: " << header->huffmanData.size() << '\n';

	std::cout << "DRI============\n";
	std::cout << "Restart Interval: " << header->restartInterval << '\n';
}

// generate all Huffman codes based on symbols from a Huffman table
void generateCodes(HuffmanTable& hTable) {
	uint32_t code = 0;
	for (uint32_t i = 0; i < 16; ++i) {
		for (uint32_t j = hTable.offsets[i]; j < hTable.offsets[i + 1]; ++j) {
			hTable.codes[j] = code;
			code += 1;
		}
		code <<= 1; // append zero to the end
	}
}

// helper class to read bits from a byte vector
class BitReader {
public:
	BitReader(const std::vector<byte>& d) : data(d) {}
public:
	// read one bit (0 or 1) or return -1 if all bits have already been read
	int readBit() {
		if (nextByte >= data.size()) {
			return -1;
		}

		int bit = (data[nextByte] >> (7 - nextBit)) & 1;
		nextBit += 1;

		if (nextBit == 8) {
			nextBit = 0;
			nextByte += 1;
		}

		return bit;
	}

	// read a variable number of bits
	// first read bit is most significant bit
	// return -1 if at any point all bits have already been read
	int readBits(const uint32_t length) {
		int bits = 0;
		for (uint32_t i = 0; i < length; ++i) {
			int bit = readBit();
			if (bit == -1) {
				bits = -1;
				break;
			}
			bits = (bits << 1) | bit;
		}
		return bits;
	}

	// if there are bits remaining,
	// advance to the 0th bit of the next byte
	void align() {
		if (nextByte >= data.size()) {
			return;
		}

		if (nextBit != 0) {
			nextBit = 0;
			nextByte += 1;
		}
	}
private:
	const std::vector<byte>& data;
	uint32_t nextByte = 0;
	uint32_t nextBit = 0;
};

// return the symbol from the Huffman table that corresponds to
// the next Huffman code read from the BitReader
byte getNextSymbol(BitReader& br, const HuffmanTable& hTable) {
	uint32_t currentCode = 0;
	for (uint32_t i = 0; i < 16; ++i) {
		int bit = br.readBit();
		if (bit == -1) {
			return -1;
		}

		currentCode = (currentCode << 1) | bit;
		for (uint32_t j = hTable.offsets[i]; j < hTable.offsets[i + 1]; ++j) {
			if (currentCode == hTable.codes[j]) {
				return hTable.symbols[j];
			}
		}
	}

	return -1;
}

// fill the coefficients of an MCU component based on Huffman codes
// read from the BitReader
bool decodeMCUComponent(
	BitReader& br, 
	int* const component,
	int& previousDC,
	const HuffmanTable& dcTable, 
	const HuffmanTable& acTable
) {
	// get the DC value for this MCU component
	byte length = getNextSymbol(br, dcTable);
	if (length == (byte) - 1) {
		std::cout << "Error - Invalid DC value\n";
		return false;
	}

	if (length > 11) {
		std::cout << "Error - DC coefficient length greater than 11\n";
		return false;
	}

	int coeff = br.readBits(length);
	if (coeff == -1) {
		std::cout << "Error - Invalid DC value\n";
		return false;
	}

	if (length != 0 && coeff < (1 << (length - 1))) {
		coeff -= (1 << length) - 1;
	}

	component[0] = coeff + previousDC;
	previousDC = component[0];

	// get the AC values for this MCU component
	uint32_t i = 1;
	while (i < 64) {
		byte symbol = getNextSymbol(br, acTable);
		if (symbol == (byte)-1) {
			std::cout << "Error - Invalid AC value\n";
			return false;
		}

		// symbol 0x00 means fill remainder of component with 0
		if (symbol == 0x00) {
			for (; i < 64; ++i) {
				component[zigZagMap[i]] = 0;
			}
			return true;
		}

		// otherwise, read next component coefficient
		byte numZeroes = symbol >> 4;
		byte coeffLength = symbol & 0x0F;
		coeff = 0;

		// symbol 0xF0 means skip 16 0's
		if (symbol == 0xF0) {
			numZeroes = 16;
		}

		if (i + numZeroes >= 64) {
			std::cout << "Error - Zero run-length exceeded MCU\n";
			return false;
		}

		for (uint32_t j = 0; j < numZeroes; ++j, ++i) {
			component[zigZagMap[i]] = 0;
		}

		if (coeffLength > 10) {
			std::cout << "Error - AC coefficient length greater than 10\n";
			return false;
		}

		if (coeffLength != 0) {
			coeff = br.readBits(coeffLength);
			if (coeff == -1) {
				std::cout << "Error - Invalid AC value\n";
				return false;
			}
			if (coeff < (1 << (coeffLength - 1))) {
				coeff -= (1 << coeffLength) - 1;
			}
			component[zigZagMap[i]] = coeff;
			i += 1;
		}
	}
	return true;
}

// decode all the Huffman data and fill all MCUs
MCU* decodeHuffmanData(Header* const header) {
	MCU* mcus = new (std::nothrow) MCU[header->mcuHeightReal * header->mcuWidthReal];

	if (mcus == nullptr) {
		std::cout << "Error - Memory error\n";
		return nullptr;
	}
	
	for (uint32_t i = 0; i < 4; ++i) {
		if (header->huffmanDCTables[i].set) {
			generateCodes(header->huffmanDCTables[i]);
		}

		if (header->huffmanACTables[i].set) {
			generateCodes(header->huffmanACTables[i]);
		}
	}

	BitReader br(header->huffmanData);

	int previousDCs[3] = { 0 };

	uint32_t restartInterval = header->restartInterval * header->horizontalSamplingFactor * header->verticalSamplingFactor;

	for (uint32_t y = 0; y < header->mcuHeight; y += header->verticalSamplingFactor) {
		for (uint32_t x = 0; x < header->mcuWidth; x += header->horizontalSamplingFactor) {
			if (restartInterval != 0 && (y * header->mcuWidthReal + x) % restartInterval == 0) {
				previousDCs[0] = 0;
				previousDCs[1] = 0;
				previousDCs[2] = 0;
				br.align();
			}

			for (uint32_t i = 0; i < header->numComponents; ++i) {
				for (uint32_t v = 0; v < header->colorComponents[i].verticalSamplingFactor; ++v) {
					for (uint32_t h = 0; h < header->colorComponents[i].horizontalSamplingFactor; ++h) {
						if (!decodeMCUComponent(
							br,
							mcus[(y + v) * header->mcuWidthReal + (x + h)][i],
							previousDCs[i],
							header->huffmanDCTables[header->colorComponents[i].huffmanDCTableID],
							header->huffmanACTables[header->colorComponents[i].huffmanACTableID]
						)) {
							delete[] mcus;
							return nullptr;
						}
					}
				}
			}
		}
	}

	return mcus;
}

// dequantize an MCU component based on a quantization table
void dequantizeMCUComponent(const QuantizationTable& qTable, int* const component) {
	for (uint32_t i = 0; i < 64; ++i) {
		component[i] *= qTable.table[i];
	}
}

// dequantize all MCUs
void dequantize(const Header* const header, MCU* const mcus) {
	for (uint32_t y = 0; y < header->mcuHeight; y += header->verticalSamplingFactor) {
		for (uint32_t x = 0; x < header->mcuWidth; x += header->horizontalSamplingFactor) {
			for (uint32_t i = 0; i < header->numComponents; ++i) {
				for (uint32_t v = 0; v < header->colorComponents[i].verticalSamplingFactor; ++v) {
					for (uint32_t h = 0; h < header->colorComponents[i].horizontalSamplingFactor; ++h) {
						dequantizeMCUComponent(
							header->quantizationTables[header->colorComponents[i].quantizationTableID],
							mcus[(y + v) * header->mcuWidthReal + (x + h)][i]
						);
					}
				}
			}
		}
	}
}

// perform 1-IDCT on all columns and rows of an MCU component
// resulting in 2-D IDCT
void inverseDCTComponent(int* const component) {
	/*
	int result[64] = { 0 };
	for (uint32_t y = 0; y < 8; ++y) {
		for (uint32_t x = 0; x < 8; ++x) {
			float sum = 0.0f;
			for (uint32_t v = 0; v < 8; ++v) {
				for (uint32_t u = 0; u < 8; ++u) {
					sum += component[v * 8 + u] *
						idctMap[u * 8 + x] *
						idctMap[v * 8 + y];
				}
			}
			result[y * 8 + x] = (int)sum;
		}
	}
	for (uint32_t i = 0; i < 64; ++i) {
		component[i] = result[i];
	}
	*/

	/*
	float result[64] = { 0 };
	for (uint32_t i = 0; i < 8; ++i) {
		for (uint32_t y = 0; y < 8; ++y) {
			float sum = 0.0f;
			for (uint32_t v = 0; v < 8; ++v) {
				sum += component[v * 8 + i] * idctMap[v * 8 + y];
			}
			result[y * 8 + i] = sum;

		}
	}
	for (uint32_t i = 0; i < 8; ++i) {
		for (uint32_t x = 0; x < 8; ++x) {
			float sum = 0.0f;
			for (uint32_t u = 0; u < 8; ++u) {
				sum += result[i * 8 + u] * idctMap[u * 8 + x];
			}
			component[i * 8 + x] = (int)sum;
		}
	}
	*/
	for (uint32_t i = 0; i < 8; ++i) {
		const float g0 = component[0 * 8 + i] * s0;
		const float g1 = component[4 * 8 + i] * s4;
		const float g2 = component[2 * 8 + i] * s2;
		const float g3 = component[6 * 8 + i] * s6;
		const float g4 = component[5 * 8 + i] * s5;
		const float g5 = component[1 * 8 + i] * s1;
		const float g6 = component[7 * 8 + i] * s7;
		const float g7 = component[3 * 8 + i] * s3;

		const float f0 = g0;
		const float f1 = g1;
		const float f2 = g2;
		const float f3 = g3;
		const float f4 = g4 - g7;
		const float f5 = g5 + g6;
		const float f6 = g5 - g6;
		const float f7 = g4 + g7;

		const float e0 = f0;
		const float e1 = f1;
		const float e2 = f2 - f3;
		const float e3 = f2 + f3;
		const float e4 = f4;
		const float e5 = f5 - f7;
		const float e6 = f6;
		const float e7 = f5 + f7;
		const float e8 = f4 + f6;

		const float d0 = e0;
		const float d1 = e1;
		const float d2 = e2 * m1;
		const float d3 = e3;
		const float d4 = e4 * m2;
		const float d5 = e5 * m3;
		const float d6 = e6 * m4;
		const float d7 = e7;
		const float d8 = e8 * m5;

		const float c0 = d0 + d1;
		const float c1 = d0 - d1;
		const float c2 = d2 - d3;
		const float c3 = d3;
		const float c4 = d4 + d8;
		const float c5 = d5 + d7;
		const float c6 = d6 - d8;
		const float c7 = d7;
		const float c8 = c5 - c6;

		const float b0 = c0 + c3;
		const float b1 = c1 + c2;
		const float b2 = c1 - c2;
		const float b3 = c0 - c3;
		const float b4 = c4 - c8;
		const float b5 = c8;
		const float b6 = c6 - c7;
		const float b7 = c7;

		component[0 * 8 + i] = b0 + b7;
		component[1 * 8 + i] = b1 + b6;
		component[2 * 8 + i] = b2 + b5;
		component[3 * 8 + i] = b3 + b4;
		component[4 * 8 + i] = b3 - b4;
		component[5 * 8 + i] = b2 - b5;
		component[6 * 8 + i] = b1 - b6;
		component[7 * 8 + i] = b0 - b7;
	}

	for (uint32_t i = 0; i < 8; ++i) {
		const float g0 = component[i * 8 + 0] * s0;
		const float g1 = component[i * 8 + 4] * s4;
		const float g2 = component[i * 8 + 2] * s2;
		const float g3 = component[i * 8 + 6] * s6;
		const float g4 = component[i * 8 + 5] * s5;
		const float g5 = component[i * 8 + 1] * s1;
		const float g6 = component[i * 8 + 7] * s7;
		const float g7 = component[i * 8 + 3] * s3;

		const float f0 = g0;
		const float f1 = g1;
		const float f2 = g2;
		const float f3 = g3;
		const float f4 = g4 - g7;
		const float f5 = g5 + g6;
		const float f6 = g5 - g6;
		const float f7 = g4 + g7;

		const float e0 = f0;
		const float e1 = f1;
		const float e2 = f2 - f3;
		const float e3 = f2 + f3;
		const float e4 = f4;
		const float e5 = f5 - f7;
		const float e6 = f6;
		const float e7 = f5 + f7;
		const float e8 = f4 + f6;

		const float d0 = e0;
		const float d1 = e1;
		const float d2 = e2 * m1;
		const float d3 = e3;
		const float d4 = e4 * m2;
		const float d5 = e5 * m3;
		const float d6 = e6 * m4;
		const float d7 = e7;
		const float d8 = e8 * m5;

		const float c0 = d0 + d1;
		const float c1 = d0 - d1;
		const float c2 = d2 - d3;
		const float c3 = d3;
		const float c4 = d4 + d8;
		const float c5 = d5 + d7;
		const float c6 = d6 - d8;
		const float c7 = d7;
		const float c8 = c5 - c6;

		const float b0 = c0 + c3;
		const float b1 = c1 + c2;
		const float b2 = c1 - c2;
		const float b3 = c0 - c3;
		const float b4 = c4 - c8;
		const float b5 = c8;
		const float b6 = c6 - c7;
		const float b7 = c7;

		component[i * 8 + 0] = b0 + b7;
		component[i * 8 + 1] = b1 + b6;
		component[i * 8 + 2] = b2 + b5;
		component[i * 8 + 3] = b3 + b4;
		component[i * 8 + 4] = b3 - b4;
		component[i * 8 + 5] = b2 - b5;
		component[i * 8 + 6] = b1 - b6;
		component[i * 8 + 7] = b0 - b7;
	}
}

// perform IDCT on all MCUs
void inverseDCT(const Header* const header, MCU* const mcus) {
	for (uint32_t y = 0; y < header->mcuHeight; y += header->verticalSamplingFactor) {
		for (uint32_t x = 0; x < header->mcuWidth; x += header->horizontalSamplingFactor) {
			for (uint32_t i = 0; i < header->numComponents; ++i) {
				for (uint32_t v = 0; v < header->colorComponents[i].verticalSamplingFactor; ++v) {
					for (uint32_t h = 0; h < header->colorComponents[i].horizontalSamplingFactor; ++h) {
						inverseDCTComponent(mcus[(y + v) * header->mcuWidthReal + (x + h)][i]);
					}
				}
			}
		}
	}
}

// convert all pixels in an MCU from YCbCr color space to RGB
void YCbCrToRGBMCU(
	const Header* const header,
	MCU& mcu,
	const MCU& cbcr,
	const uint32_t v,
	const uint32_t h
) {
	for (uint32_t y = 7; y < 8; --y) {
		for (uint32_t x = 7; x < 8; --x) {
			const uint32_t pixel = y * 8 + x;
			const uint32_t cbcrPixelRow = y / header->verticalSamplingFactor + 4 * v;
			const uint32_t cbcrPixelColumn = x / header->horizontalSamplingFactor + 4 * h;
			const uint32_t cbcrPixel = cbcrPixelRow * 8 + cbcrPixelColumn;

			int r = mcu.y[pixel] + 1.402f * cbcr.cr[cbcrPixel] + 128;
			int g = mcu.y[pixel] - 0.344f * cbcr.cb[cbcrPixel] - 0.714f * cbcr.cr[cbcrPixel] + 128;
			int b = mcu.y[pixel] + 1.772f * cbcr.cb[cbcrPixel] + 128;

			if (r < 0) r = 0;
			if (r > 255) r = 255;
			if (g < 0) g = 0;
			if (g > 255) g = 255;
			if (b < 0) b = 0;
			if (b > 255) b = 255;

			mcu.r[pixel] = r;
			mcu.g[pixel] = g;
			mcu.b[pixel] = b;
		}
	}
}

// convert all pixels from YCbCr color space to RGB
void YCbCrToRGB(const Header* const header, MCU* const mcus) {
	for (uint32_t y = 0; y < header->mcuHeight; y += header->verticalSamplingFactor) {
		for (uint32_t x = 0; x < header->mcuWidth; x += header->horizontalSamplingFactor) {
			const MCU& cbcr = mcus[y * header->mcuWidthReal + x];
			for (uint32_t v = header->verticalSamplingFactor - 1; v < header->verticalSamplingFactor; --v) {
				for (uint32_t h = header->horizontalSamplingFactor - 1; h < header->horizontalSamplingFactor; --h) {
					MCU& mcu = mcus[(y + v) * header->mcuWidthReal + (x + h)];
					YCbCrToRGBMCU(header, mcu, cbcr, v, h);
				}
			}
		}
	}
}

// helper function to write a 4-byte integer in little-endian
void putInt(std::ofstream& file, const uint32_t v) {
	file.put((v >> 0) & 0xFF);
	file.put((v >> 8) & 0xFF);
	file.put((v >> 16) & 0xFF);
	file.put((v >> 24) & 0xFF);
}

// helper function to write a 2-byte short integer in little-endian
void putShort(std::ofstream& file, const uint32_t v) {
	file.put((v >> 0) & 0xFF);
	file.put((v >> 8) & 0xFF);
}

// write all the pixels in the MCUs to a BMP file
void writeBMP(const Header* const header, const MCU* const mcus, const std::string& filename) {
	std::ofstream file = std::ofstream(filename, std::ios::out | std::ios::binary);
	if (!file.is_open()) {
		std::cout << "Error - Error opening output file\n";
		return;
	}

	const uint32_t paddingSize = header->width % 4;
	const uint32_t size = 14 + 12 + header->height * header->width * 3 + paddingSize * header->height;

	file.put('B');
	file.put('M');
	putInt(file, size);
	putInt(file, 0);
	putInt(file, 0x1A);
	putInt(file, 12);
	putShort(file, header->width);
	putShort(file, header->height);
	putShort(file, 1);
	putShort(file, 24);

	for (int32_t y = static_cast<int32_t>(header->height) - 1; y >= 0; --y) {
		const uint32_t mcuRow = y / 8;
		const uint32_t pixelRow = y % 8;
		for (uint32_t x = 0; x < header->width; ++x) {
			const uint32_t mcuColumn = x / 8;
			const uint32_t pixelColumn = x % 8;
			const uint32_t mcuIndex = mcuRow * header->mcuWidthReal + mcuColumn;
			const uint32_t pixelIndex = pixelRow * 8 + pixelColumn;
			
			file.put(mcus[mcuIndex].b[pixelIndex]);
			file.put(mcus[mcuIndex].g[pixelIndex]);
			file.put(mcus[mcuIndex].r[pixelIndex]);
		}

		for (uint32_t i = 0; i < paddingSize; ++i) {
			file.put(0);
		}
	}

	file.close();
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cout << "Error - Invalid arguments\n";
		return 1;
	}

	for (int i = 1; i < argc; ++i) {
		const std::string filename(argv[i]);

		// read header
		Header* header = readJPG(filename);

		// validate header
		if (header == nullptr) continue;
		if (!header->isValid) {
			std::cout << "Error - Invalid JPG\n";
			delete header;
			continue;
		}

		printHeader(header);

		// decode Huffman data
		MCU* mcus = decodeHuffmanData(header);
		if (mcus == nullptr) {
			delete header;
			continue;
		}

		// dequantize MCU coefficients
		dequantize(header, mcus);

		// Inverse Discrete Cosine Transform
		inverseDCT(header, mcus);

		// Color conversion
		YCbCrToRGB(header, mcus);

		// write BMP file
		const size_t pos = filename.find_last_of('.');
		const std::string outFilename = (pos == std::string::npos) ? (filename + ".bmp") : (filename.substr(0, pos) + ".bmp");
		writeBMP(header, mcus, outFilename);

		delete[] mcus;
		delete header;
	}

	return 0;
}