#include <Midiboy.h>
#include <midi_serialization.h>

#define FONT_WIDTH MIDIBOY_FONT_5X7::WIDTH
#define FONT MIDIBOY_FONT_5X7::DATA_P

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
static uint8_t g_cc = 3;
static uint8_t g_latestValue = 0;
static uint8_t g_values[128];
static uint8_t g_index = 0;
static uint16_t g_updateMs = 8;

void setup()
{
	Midiboy.begin();
	print(0, 0, "CC:", 3, 128, false);
	drawGraph();
}

void print(uint8_t x, uint8_t line, const char *text, uint8_t n, uint8_t maxWidth, bool inverted)
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

static void processEvent(midi_event_t event, MidiStream &dinStream, MidiStream &usbStream)
{
	if ((event.m_event & 0x0f) == 0x0b)
	{
		// CC event detected.
		if (event.m_data[1] == g_cc)
		{
			// Rescale from 0-127 range to 0-55 which we can display.
			g_latestValue = (event.m_data[2] * 56u) >> 7u;
		}
	}
	// Forward the event to all outputs.
	dinStream.writeEvent(event);
	//usbStream.writeEvent(event);
}

static void processEvents(MidiStream &dinStream, MidiStream &usbStream)
{
	midi_event_t e;
	while (dinStream.readEvent(e))
		processEvent(e, dinStream, usbStream);
	while (usbStream.readEvent(e))
		processEvent(e, dinStream, usbStream);
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

#if 0
				if (b > a)
				{
					if (j == v2/8)
					{
						d = 1 << (v2&7);
						d |= d-1;
					}
					else if (j < v2/8 && j > v/8)
					{
						d = 0xff;
					}
					else if (j == v/8)
					{
						d = 1 << (v&7);
						d |= ~(d-1);
					}
				}
				else if (b < a)
				{
					//if (j == v/8)
					//{
					//	d = 1 << (v&7);
					//}
				}
				else if (b == j)
				{
					if (v < v2)
					{
						d = 1 << (v2&7);
						d |= d-(1 << (v&7));
					}
					else if (v > v2)
					{
						d = 1 << (v&7);
						d |= d-(1 << (v2&7));
					}
					else
					{
						d = 1 << (v&7);
					}
				}
			}
#endif

			Midiboy.drawBits(d, 1, false);
		}

		// Draw the latest value.
		uint8_t v = g_values[(g_index+127)&0x7f];
		Midiboy.drawBits(j == v/8 ? (1 << (v&7)) : 0, 1, false);
	}
}

void loop()
{
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
}
