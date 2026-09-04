// Gradient -- minimal esp32-spwm-hub75 example.
//
// Draws a moving diagonal gradient with a few primitives on top, to show both
// the framebuffer and the drawing calls.
//
// Wiring and geometry default to a 128x64 P2.5 ICND1065L panel; override with
// -D flags (see spwm_config.h). No level shifting is needed: 3.3 V straight
// into the panel's 74HC245 works, and adding proper 5 V buffering measurably
// changes nothing.

#include <spwm_hub75.h>
#include <math.h>

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!spwm_begin()) {
    Serial.println("spwm_begin failed -- not enough internal DMA-capable RAM?");
    // Nothing is driving the panel now. Do not carry on and draw into it.
    while (true) delay(1000);
  }

  spwm_set_brightness(25);   // full drive is ~1.2 A and visibly harsh

  Serial.printf("clock %.2f MHz, multiplex %u Hz, frame %u words\n",
                spwm_clock_hz() / 1e6f, spwm_multiplex_hz(),
                spwm_frame_words());
}

void loop() {
  const float t = millis() / 1000.0f;

  for (int y = 0; y < spwm_height(); y++) {
    for (int x = 0; x < spwm_width(); x++) {
      float u = (x + y) / (float) (spwm_width() + spwm_height());
      u += 0.25f * sinf(t * 0.7f);
      u -= floorf(u);
      spwm_set_pixel(x, y, spwm_rgb((uint8_t) (255 * u),
                                    (uint8_t) (255 * (1.0f - u)),
                                    (uint8_t) (128 + 127 * sinf(t))));
    }
  }

  spwm_draw_rect(2, 2, spwm_width() - 4, spwm_height() - 4, spwm_rgb(255, 255, 255));
  spwm_fill_circle(spwm_width() / 2, spwm_height() / 2, 8, spwm_rgb(0, 0, 0));
  spwm_draw_circle(spwm_width() / 2, spwm_height() / 2, 10, spwm_rgb(255, 255, 0));

  spwm_show();
  delay(16);
}
