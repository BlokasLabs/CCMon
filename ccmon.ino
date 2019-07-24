/*
 * CCMon - CC Plotter sketch for Midiboy.
 * Copyright (C) 2019  Vilniaus Blokas UAB, https://blokas.io/midiboy
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <Midiboy.h>
#include <midi_serialization.h>

#define FONT_WIDTH MIDIBOY_FONT_5X7::WIDTH
#define FONT MIDIBOY_FONT_5X7::DATA_P
#define CHAR_WIDTH (FONT_WIDTH+1)

class MidiStream
{
public:
	MidiStream(Stream &stream, int id)
		:m_stream(stream)
		,m_decoder(id)
	{
	}

	bool readEvent(midi_event_t &result)
	{
		while (m_stream.available())
		{
			if (m_decoder.process(m_stream.read(), result))
				return true;
		}

		return false;
	}

	void writeEvent(const midi_event_t &event)
	{
		uint8_t data[3];
		uint8_t n = UsbToMidi::process(event, data);
		if (n) m_stream.write(data, n);
	}

private:
	Stream &m_stream;
	MidiToUsb m_decoder;
};

enum
{
	ID_DIN5 = 0,
	ID_USB  = 1
};

static MidiStream g_dinMidi(Midiboy.dinMidi(), ID_DIN5);
static MidiStream g_usbMidi(Midiboy.usbMidi(), ID_USB);
static uint8_t g_cc = 0xff;
static uint8_t g_latestValue = 0;
static uint8_t g_values[128];
static uint8_t g_index = 0;
static uint8_t g_updateMs = 8;

static void print(uint8_t x, uint8_t line, const char *text, uint8_t n, uint8_t maxWidth, bool inverted)
{
	Midiboy.setDrawPosition(x, 7-(line&7));
	uint8_t width = min(n*(FONT_WIDTH+1), maxWidth);
	uint8_t spaces = maxWidth - width;
	uint8_t counter = 0;
	const uint8_t *p = NULL;
	while (width-- && n)
	{
		switch (counter)
		{
		case 0:
			p = &FONT[(*text++ - ' ') * FONT_WIDTH];
			break;
		case FONT_WIDTH:
			Midiboy.drawSpace(1, inverted);
			--n;
			counter = 0;
			continue;
		default:
			break;
		}

		Midiboy.drawBitmap_P(p++, 1, inverted);
		++counter;
	}
	if (spaces > 0)
	{
		Midiboy.drawSpace(spaces, inverted);
	}
}

static uint8_t numToStr(char str[6], uint16_t num)
{
	uint8_t n = 0;
	uint16_t tmp = num;
	while (tmp)
	{
		++n;
		tmp /= 10;
	}

	str[n] = '\0';
	uint8_t i=n;
	while (i--)
	{
		str[i] = num % 10 + '0';
		num /= 10;
	}
	return n;
}

static uint8_t printNumber(uint8_t x, uint8_t line, uint8_t number, uint8_t maxWidth, bool inverse)
{
	char str[6];
	uint8_t n = numToStr(str, number);
	print(x, line, str, n, maxWidth, inverse);
	return n;
}

static void printInfoLine()
{
	print(0, 0, "CC#:", 4, 128, false);
	if (g_cc != 0xff)
	{
		printNumber(CHAR_WIDTH*5, 0, g_cc, CHAR_WIDTH*4, false);
	}
	else
	{
		print(CHAR_WIDTH*5, 0, "Auto", 4, CHAR_WIDTH*4, false);
	}

	char str[6];
	uint8_t n = numToStr(str, g_updateMs*128u);
	print(128-CHAR_WIDTH*(2+n), 0, str, n, CHAR_WIDTH*n, false);
	print(128-CHAR_WIDTH*2, 0, "ms", 2, CHAR_WIDTH*2, false);
}

void setup()
{
	Midiboy.begin();
	printInfoLine();
	drawGraph();
	Midiboy.setButtonRepeatMs(50);
}

static void processEvent(midi_event_t event)
{
	if ((event.m_event & 0x0f) == 0x0b)
	{
		// CC event detected.
		if (g_cc == 0xff)
		{
			// In case we were in Auto mode, switch to first CC detected.
			g_cc = event.m_data[1];
			printInfoLine();
		}
		if (event.m_data[1] == g_cc)
		{
			// Rescale from 0-127 range to 0-55 which we can display.
			g_latestValue = (event.m_data[2] * 56u) >> 7u;
		}
	}
}

static void processEvents(MidiStream &dinStream, MidiStream &usbStream)
{
	midi_event_t e;
	while (dinStream.readEvent(e))
	{
		processEvent(e);
		dinStream.writeEvent(e);
	}
	while (usbStream.readEvent(e))
	{
		processEvent(e);
		usbStream.writeEvent(e);
	}
}

static void drawGraph()
{
	for (int j=0; j<7; ++j)
	{
		Midiboy.setDrawPosition(0, j);
		for (int i=0; i<127; ++i)
		{
			// Values are in range 0-55.
			uint8_t v1 = g_values[(g_index+i)&0x7f];
			uint8_t v2 = g_values[(g_index+i+1)&0x7f];

			div_t a, b;
			if (v1 <= v2)
			{
				a = div(v1, 8);
				b = div(v2, 8);
			}
			else
			{
				a = div(v2, 8);
				b = div(v1, 8);
			}

			uint8_t d;

			if (j < a.quot)
			{
				d = 0x00;
			}
			else if (j > a.quot)
			{
				if (j == b.quot)
				{
					d = 1 << (b.rem&7);
					d |= d-1;
				}
				else if (j > b.quot)
				{
					d = 0x00;
				}
				else
				{
					d = 0xff;
				}
			}
			else
			{
				if (j == b.quot)
				{
					d = 1 << (b.rem&7);
					d |= d-(1 << (a.rem&7));
				}
				else
				{
					d = 1 << (a.rem&7);
					d |= ~(d-1);
				}
			}

			Midiboy.drawBits(d, 1, false);
		}

		// Draw the latest value.
		uint8_t v = g_values[(g_index+127)&0x7f];
		Midiboy.drawBits(j == v/8 ? (1 << (v&7)) : 0, 1, false);
	}
}

void loop()
{
	Midiboy.think();
	processEvents(g_dinMidi, g_usbMidi);

	static unsigned long lastUpdatedAt = millis();

	bool hadUpdate = false;
	unsigned long now = millis();
	while (now - lastUpdatedAt > g_updateMs)
	{
		hadUpdate = true;
		g_values[g_index] =  g_latestValue;
		g_index = (g_index+1)&0x7f;
		lastUpdatedAt += g_updateMs;
	}
	if (hadUpdate)
		drawGraph();

	MidiboyInput::Event event;
	while (Midiboy.readInputEvent(event))
	{
		if (event.m_type != MidiboyInput::EVENT_DOWN)
			continue;

		switch (event.m_button)
		{
		case MidiboyInput::BUTTON_A:
			g_cc = 0xff;
			g_latestValue = 0;
			printInfoLine();
			break;
		case MidiboyInput::BUTTON_UP:
			g_cc = (g_cc+1)&0x7f;
			g_latestValue = 0;
			printInfoLine();
			break;
		case MidiboyInput::BUTTON_DOWN:
			g_cc = g_cc != 0xff ? (g_cc+128-1)&0x7f : 127;
			g_latestValue = 0;
			printInfoLine();
			break;
		case MidiboyInput::BUTTON_LEFT:
			g_updateMs = max(g_updateMs-1, 1);
			printInfoLine();
			break;
		case MidiboyInput::BUTTON_RIGHT:
			g_updateMs = min(g_updateMs+1, 250);
			printInfoLine();
			break;
		}
	}
}
