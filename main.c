/* This file is licensed under GPL3, as a derivative work from fabooh. */
/* Developed by Dave Bender, https://analog10.com  */
#include <stdint.h>
#include <msp430.h>

#include "protocol.h"

/* Configuration: */
#ifndef BAUD
#define BAUD 4800
#endif

/* The loop takes 4 cycles per iteration, so divide delay by 4. */
#define TICKS_PER_BIT (F_CPU / BAUD - 18)
#define BIT_DELAY (TICKS_PER_BIT >> 2)
#define BIT_COUNT 10

enum {
	TX_BIT = BIT2
	,RX_BIT = BIT1
};

/* Possible states. */
enum {
	/** @brief Host is sending configuration data (target frequency). */
	ST_CONFIGURE
	,ST_SURVEY
	,ST_DIAGNOSTIC
	,ST_FINISHED
};

/* This has a very special encoding. */
volatile uint16_t one_count;

/* Host configured target frequency. */
uint32_t cfg_target_frequency = 16000000UL;
uint32_t last_estimate = 0;
uint16_t last_var = 0;

/* Listening state to count cycles. */
volatile uint16_t cnts[BIT_COUNT];
volatile unsigned bit_indx;

/* Current clock settings when listening for host signal. */
uint8_t dco;
uint8_t bcs;

/* Current operating state. */
uint8_t state = ST_CONFIGURE;


/* Increment the dco and bcs values. */
uint8_t increment_values(uint8_t last){

	if(OUT_DCO_INCREMENT == last){
		/* Changing mod bits at maximum DCO has no effect, but
		 * for simplicity sake just do it anyway.*/
		if((dco & 0xE0) == 0xE0){
			++dco;
			return OUT_MOD_INCREMENT;
		}
		dco += 0x20;
		return OUT_DCO_INCREMENT;
	}
	else if(OUT_DCO_DECREMENT == last){
		/* Local minimum is in the MOD bits.
		 * Report net MOD decrease. */
		dco += 0x20;
		--dco;
		return OUT_MOD_DECREMENT;
	}

	if(OUT_MOD_INCREMENT == last){
		if((dco & 0x1F) == 0x1F)
			return OUT_MAX;
		++dco;
		return OUT_MOD_INCREMENT;
	}
	else if(OUT_MOD_DECREMENT == last){
		/* Go back to the previous mod value. */
		++dco;
		return OUT_MOD_INCREMENT;
	}

	/* Otherwise, this is a bad function call. */
	return OUT_OP_ERR;
}

/* Decrement the dco and bcs values. */
uint8_t decrement_values(uint8_t last){

	if(OUT_DCO_DECREMENT == last){
		/* Check DCO bound. */
		if((dco & 0xE0) == 0x00){
			/* Decrement the mod value. */
			--dco;
			return OUT_MOD_DECREMENT;
		}
		dco -= 0x20;
		return OUT_DCO_DECREMENT;
	}
	else if(OUT_DCO_INCREMENT == last){
		/* Local minimum is in the MOD bits.
		 * Report net MOD increase. */
		dco -= 0x20;
		++dco;
		return OUT_MOD_INCREMENT;
	}

	if(OUT_MOD_DECREMENT == last){
		if((dco & 0x1F) == 0x00)
			return OUT_MIN;
		--dco;
		return OUT_MOD_DECREMENT;
	}
	else if(OUT_MOD_INCREMENT == last){
		/* Go back to the previous mod value. */
		--dco;
		return OUT_MOD_DECREMENT;
	}

	/* Otherwise, this is a bad function call. */
	return OUT_OP_ERR;
}

/* Bit-banged serial. Yes this is ugly.
 * Please do not copy this function unless you
 * really do not have a better option.
 *
 * Make sure the clock is at 1MHz before using this function!
 *
 * The delay code I lifted from Energia I believe. */
void xmit_char(uint8_t val){
	uint16_t tx_byte = val;

	/* Add the start bit. */
	tx_byte <<= 1;

	/* Add 1 stop bit. */
	tx_byte |= 0x200;

	while(tx_byte){
		P1OUT = (tx_byte & 0x1) ? (TX_BIT | RX_BIT) : RX_BIT;

		/* Use tmp as a delay counter now. */
		register uint16_t tmp = BIT_DELAY;

		/* Busy wait */
		__asm__ __volatile__ (
				/* even steven */
				"L1: nop \n\t"   
				/* 1 instruction */
				"dec.w %[tmp] \n\t"
				/* 2 instructions */
				"jnz L1 \n\t"
				: [tmp] "=r" (tmp) : "[tmp]" (tmp)
				);

		tx_byte >>= 1;
	}
}

int main(void){
	/* Disable watchdog. */
	WDTCTL = WDTPW | WDTHOLD;

	/* Setup with factory 1 MHz calibration. */
	BCSCTL1 = CALBC1_1MHZ;
	DCOCTL = CALDCO_1MHZ;
	BCSCTL2 &= ~(DIVS_0);
	BCSCTL3 |= LFXT1S_2; 

	/* Initialize to almost center of frequency range.
	 * Start at lowest frequency value. */
	dco = 0x0;
	bcs = (BCSCTL1 & 0xF0) | 0x7;

	/* Disable external crystal. */
	P2SEL = 0;

	/* Setup RX_BIT, our UART input bit. */
	P1DIR = TX_BIT;
	P1OUT = TX_BIT | RX_BIT;
	P1REN = ~TX_BIT;
	P1IES = RX_BIT;

	/* Enable SMCLK on P1.4 */
	P1DIR |= BIT4;
	P1SEL = BIT4;

	P1IE = RX_BIT;
	P1IFG = 0;

	uint8_t rsel_search = 1;

	/* Don't have a last estimate, so set it to max (frequency itself) */
	uint32_t estimated_freq = 0;
	uint32_t last_diff = 0xFFFFFFFFL;
	uint8_t tx = OUT_START;
	while(1){

		BCSCTL1 = CALBC1_1MHZ;
		DCOCTL = CALDCO_1MHZ;
		xmit_char(tx);

		/* If we are finished, stop listening to the RX_BIT
		 * and get out of this loop. */
		if(ST_FINISHED == state){
			/* Frequency estimate. */
			xmit_char(last_estimate & 0xFF);
			last_estimate >>= 8;
			xmit_char(last_estimate & 0xFF);
			last_estimate >>= 8;
			xmit_char(last_estimate & 0xFF);
			last_estimate >>= 8;
			xmit_char(last_estimate & 0xFF);

			/* Estimated error from true frequency. */
			xmit_char(last_var & 0xFF);
			last_var >>= 8;
			xmit_char(last_var & 0xFF);
			last_var >>= 8;
			xmit_char(last_var & 0xFF);
			last_var >>= 8;
			xmit_char(last_var & 0xFF);

			/* Output the calibrated values at 9600 baud. */
			xmit_char(dco);
			xmit_char(bcs);

			state = ST_CONFIGURE;
		}

		while(1){
			/* Prepare to listen at the test frequency. */
			BCSCTL1 = (BCSCTL1 & 0xF0) | 0x7;
			DCOCTL = 0;
			BCSCTL1 = bcs;
			DCOCTL = dco;
			bit_indx = 0;
			one_count = 0;
			for(unsigned i = 0; i < BIT_COUNT; ++i)
				cnts[i] = 0;
			P1IFG = 0;

			/* Now wait for host char */
			__eint();
			LPM0;
			__dint();

			if(ST_CONFIGURE == state){
				/* Assume good. */
				uint8_t reply = OUT_RX_ACK | one_count;

				/* one_count is now relevant. */
				switch(one_count){
					case 1:
						/* Shift a 1 into req. */
						cfg_target_frequency <<= 1;
						cfg_target_frequency |= 1;
						break;

					case 2:
						/* Shift a 0 into req. */
						cfg_target_frequency <<= 1;
						break;

					case 3:
						/* Initiate DCO calibration. */
						state = ST_SURVEY;
						break;

					case 4:
						/* Reset to 0. */
						cfg_target_frequency = 0;
						last_diff = 0xFFFFFFFFL;
						estimated_freq = 0;
						last_var = 0;
						break;

					case 5:
						/* Reserved behaviour. Maybe change to a different register?
						 * Or echo register? who knows. */
						break;

					default:
						reply = OUT_RX_ERR | (one_count & 0xF);
						break;
				}

				BCSCTL1 = CALBC1_1MHZ;
				DCOCTL = CALDCO_1MHZ;
				xmit_char(reply);
			}
			else{
				/* If received all 10 transitions then proceed to estimate. */
				if(bit_indx == BIT_COUNT)
					break;

				/* Otherwise timed out while receiving char. Let the host know. */
				BCSCTL1 = CALBC1_1MHZ;
				DCOCTL = CALDCO_1MHZ;
				xmit_char(OUT_RX_ERR);
			}
		}

		/* Lifted from goldilocks.cpp */
    uint16_t i, avg=0;
		for (i = 2; i < bit_indx; i++) {
			unsigned bdur = cnts[i] - cnts[i - 1];
			avg += bdur;
		}

		/* division filters the "noise" from time difference readings. */
    avg /= BIT_COUNT - 2;


		/* Extrapolate what target frequency would be based on this sampling. */
		estimated_freq = avg;
		estimated_freq *= BAUD;

		/* Compare estimate and target; calculate diff accordingly */
		uint8_t estimate_greater =
			(estimated_freq > cfg_target_frequency) ? 1 : 0;
		uint32_t diff = estimate_greater
			? (estimated_freq - cfg_target_frequency)
			: (cfg_target_frequency - estimated_freq);

		uint16_t var = 0;
		for(i = 2; i < bit_indx; i++) {
			uint16_t d = cnts[i] - cnts[i - 1];
			d = d > avg ? d - avg : avg - d;
			var += d * d;
		}

		/* Scanning increasing RSEL ranges to find optimal RSEL value. */
		if(1 == rsel_search){

			/* If estimate is greater, then established rsel range. */
			if(estimate_greater){
				if(0 == dco){
					/* This is exceptional. The previous RSEL range should overlap.
					 * If the diff is not less than the baud rate then we have a problem.*/
					if(diff < BAUD){
						/* Lucky, found the solution. */
						last_diff = diff;
						last_estimate = estimated_freq;
						last_var = var;

						tx = OUT_FINISH;
						state = ST_FINISHED;
					}
					else{
						/* Problem... */
						tx = OUT_OP_ERR;
						state = ST_FINISHED;
					}
					continue;
				}

				/* Established the RSEL, go onto fine adjustment. */
				rsel_search = 0;
				tx = OUT_DCO_DECREMENT;
			}
			else{
				/* Checking dco extremes.. */
				if(0 == dco){
					dco = 0xFF;
					tx = OUT_DCO_MAXED;
				}
				else{
					++bcs;
					dco = 0;
					tx = OUT_RSEL_INCREMENT;
				}

				last_diff = diff;
				last_estimate = estimated_freq;
				last_var = var;
				continue;
			}
		}

		/* Since estimate is scaled by BAUD then
		 * we should be able to have an error less than BAUD.
		 * If last estimate was better than current estimate, declare it
		 * the winner.
		 * */
		if(last_diff < BAUD && diff > last_diff){
			/* Last estimate was better, reverse what we did. */
			if((tx & 0xF0) == OUT_MOD_INCREMENT)
				decrement_values(tx);
			else if((tx & 0xF0) == OUT_MOD_DECREMENT)
				increment_values(tx);
			else{
				/* FIXME If we didn't increment or decrement, wtf did we do? */
			}

			tx = OUT_FINISH;
			state = ST_FINISHED;
		}
		else{
			last_diff = diff;
			last_estimate = estimated_freq;
			last_var = var;

			/* Getting there, take action based on sign of diff. */
			if(estimate_greater){
				/* Slow down the DCO. */
				tx = decrement_values(tx);
			}
			else{
				/* Speed up the DCO. */
				tx = increment_values(tx);
			}

			if(OUT_MAX == tx || OUT_MIN == tx){
				/* Reached extreme, cannot go any further. */
				state = ST_FINISHED;
			}
		}
	}

	/* Run the processor at the new frequency, so that the
	 * SMCLK frequency can be measured by oscope. */
	BCSCTL1 = (BCSCTL1 & 0xF0) | 0x7;
	DCOCTL = 0;
	BCSCTL1 = bcs;
	DCOCTL = dco;
	while(1);
}

__attribute((interrupt(PORT1_VECTOR)))
void Port_1(void){
	register uint16_t v = TAR;

  if(!bit_indx){
		/* Reset the timer.
		 * Continuous up. */
		TA0CTL = TASSEL_2 | ID_0 | MC_2 | TACLR | TAIE;
  }
	else{
  	cnts[bit_indx] = v;
		if(P1IN & RX_BIT)
			++one_count;
	}

	/* Switch edge trigger so we get called on next transition */
	P1IES ^= RX_BIT;

  if ( ++bit_indx == BIT_COUNT ) { // last one? wake main line
		/* Disable the timer. */
		TA0CTL = MC_0;
		P1IES = RX_BIT;
    LPM0_EXIT;
  }

	P1IFG = 0;
}

__attribute__((interrupt(TIMER0_A1_VECTOR)))
void timeout_detect(void){
	/* Clear interrupt flag. */
  volatile uint16_t resetTAIVIFG;
  resetTAIVIFG=TA0IV;(void)resetTAIVIFG;

	if(bit_indx < BIT_COUNT){
		/* Timed out. At 16MHz, the timeout is 4ms, which should be plenty.
		 * Reset edge direction and wake up cpu. */
		P1IES = RX_BIT;
    LPM0_EXIT;
	}
}
