/*
 * Copyright 2010 by Adam Mayer	 <adam@makerbot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include "ExtruderBoard.hh"
#include "HeatingElement.hh"
#include "ExtruderMotor.hh"
#include "Configuration.hh"
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <avr/sfr_defs.h>
#include <avr/io.h>

ExtruderBoard ExtruderBoard::extruderBoard;

Pin channelA(PortC,1);
Pin channelB(PortB,3);
Pin channelC = FAN_ENABLE_PIN;

ExtruderBoard::ExtruderBoard() :
		micros(0L),
		extruder_thermistor(THERMISTOR_PIN,0),
		platform_thermistor(PLATFORM_PIN,1),
		extruder_heater(extruder_thermistor,extruder_element),
		platform_heater(platform_thermistor,platform_element)
{
}

// Turn on/off PWM for channel A.
void pwmAOn(bool on) {
	if (on) {
		TIMSK2 = 0b00000101;
	} else {
		TIMSK2 = 0b00000000;
	}
}

// Turn on/off PWM for channel B.
void pwmBOn(bool on) {
	if (on) {
		TCCR2A = 0b10000011;
	} else {
		TCCR2A = 0b00000011;
	}
}

void ExtruderBoard::reset() {
	initExtruderMotor();
	// Timer 1 is for microsecond-level timing.
	// CTC mode, interrupt on OCR1A, no prescaler
	TCCR1A = 0x00;
	TCCR1B = 0x09;
	TCCR1C = 0x00;
	OCR1A = INTERVAL_IN_MICROSECONDS * 16;
	TIMSK1 = 0x02; // turn on OCR1A match interrupt
	// TIMER2 is used to PWM mosfet channel B on OC2A, and channel A on
	// PC1 (using the OC2B register).
	channelA.setValue(false);
	channelA.setDirection(true); // set channel A as output
	channelB.setValue(false);
	channelB.setDirection(true); // set channel B as output
	channelC.setValue(false);
	channelC.setDirection(true); // set channel C as output
	TCCR2A = 0b10000011;
	TCCR2B = 0b00000010; // prescaler 1/8
	OCR2A = 0;
	OCR2B = 0;
	// We use interrupts on OC2B and OVF to control channel A.
	TIMSK2 = 0b00000101;
	extruder_thermistor.init();
	platform_thermistor.init();
	extruder_heater.set_target_temperature(0);
	platform_heater.set_target_temperature(0);
	getHostUART().enable(true);
	getHostUART().in.reset();
}

void ExtruderBoard::setMotorSpeed(int16_t speed) {
	setExtruderMotor(speed);
}

micros_t ExtruderBoard::getCurrentMicros() {
	micros_t micros_snapshot;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		micros_snapshot = micros;
	}
	return micros_snapshot;
}

/// Run the extruder board interrupt
void ExtruderBoard::doInterrupt() {
	micros += INTERVAL_IN_MICROSECONDS;
}

void ExtruderBoard::setFan(bool on) {
	channelC.setValue(on);
}

/// Timer one comparator match interrupt
ISR(TIMER1_COMPA_vect) {
	ExtruderBoard::getBoard().doInterrupt();
}

void ExtruderHeatingElement::setHeatingElement(uint8_t value) {
	if (value > 128) value = 255;
	if (value > 0) value = 128;
	if (value == 0 || value == 255) {
		pwmBOn(false);
		channelB.setValue(value == 255);
	} else {
		OCR2A = value;
		pwmBOn(true);
	}
}



void BuildPlatformHeatingElement::setHeatingElement(uint8_t value) {
	if (value == 0 || value == 255) {
		pwmAOn(false);
		channelA.setValue(value == 255);
	} else {
		OCR2B = value;
		pwmAOn(true);
	}
}

ISR(TIMER2_OVF_vect) {
	if (OCR2B != 0) {
		channelA.setValue(true);
	}
}

ISR(TIMER2_COMPB_vect) {
	channelA.setValue(false);
}
