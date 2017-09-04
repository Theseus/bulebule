#include <stdio.h>
#include <errno.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/adc.h>


int _write(int file, char *ptr, int len);
volatile uint16_t sensor_1, sensor_2, sensor_3, sensor_4 = 0;

/**
 * @brief Initial clock setup.
 *
 * Use the External High Speed clock (HSE), at 8 MHz, and set the SYSCLK
 * at 72 MHz (the maximum allowed when using the external crystal/resonator).
 * This output frequency is possible thanks to the Phase Locked Loop (PLL)
 * multiplier.
 *
 * Enable required clocks for the GPIOs and timers as well.
 */
static void setup_clock(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Bluetooth */
	rcc_periph_clock_enable(RCC_USART3);

	/* Encoders */
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_TIM3);
	rcc_periph_clock_enable(RCC_TIM4);
	
	/*Adc*/
	rcc_periph_clock_enable(RCC_ADC1);
	
}


/**
 * @brief Initial GPIO configuration.
 *
 * Set GPIO modes and initial states.
 */
static void setup_gpio(void)
{
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
	gpio_clear(GPIOC, GPIO13);

	/* Motor driver */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL,
		      GPIO12 | GPIO13 | GPIO14 | GPIO15);
	gpio_clear(GPIOB, GPIO12 | GPIO13 | GPIO14 | GPIO15);

	/*ADC*/
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, 
		      GPIO_CNF_INPUT_ANALOG,
		      GPIO3 | GPIO4 | GPIO5 | GPIO6 );
}


/**
 * @brief Setup USART for bluetooth communication.
 */
static void setup_usart(void)
{
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART3_TX);

	usart_set_baudrate(USART3, 921600);
	usart_set_databits(USART3, 8);
	usart_set_stopbits(USART3, USART_STOPBITS_1);
	usart_set_parity(USART3, USART_PARITY_NONE);
	usart_set_flow_control(USART3, USART_FLOWCONTROL_NONE);
	usart_set_mode(USART3, USART_MODE_TX);

	usart_enable(USART3);
}


/**
 * @brief Setup PWM for the motor drivers.
 *
 * TIM3 is used to generate both PWM signals (left and right motor):
 *
 * - Edge-aligned, up-counting timer.
 * - Prescale to increment timer counter at 24 MHz.
 * - Set PWM frequency to 24 kHz.
 * - Configure channels 3 and 4 as output GPIOs.
 * - Set output compare mode to PWM1 (output is active when the counter is
 *   less than the compare register contents and inactive otherwise.
 * - Reset output compare value (set it to 0).
 * - Enable channels 3 and 4 outputs.
 * - Enable counter for TIM3.
 *
 * @see Reference manual (RM0008) "TIMx functional description" and in
 * particular "PWM mode" section.
 */
static void setup_pwm(void)
{
	timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT,
		       TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);

	timer_set_prescaler(TIM3, 3);
	timer_set_repetition_counter(TIM3, 0);
	timer_enable_preload(TIM3);
	timer_continuous_mode(TIM3);
	timer_set_period(TIM3, 1000);

	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO_TIM3_CH3 | GPIO_TIM3_CH4);

	timer_set_oc_mode(TIM3, TIM_OC3, TIM_OCM_PWM1);
	timer_set_oc_mode(TIM3, TIM_OC4, TIM_OCM_PWM1);
	timer_set_oc_value(TIM3, TIM_OC3, 0);
	timer_set_oc_value(TIM3, TIM_OC4, 0);
	timer_enable_oc_output(TIM3, TIM_OC3);
	timer_enable_oc_output(TIM3, TIM_OC4);

	timer_enable_counter(TIM3);
}


/**
 * @brief Configure timer to read a quadrature encoder.
 *
 * - Set the Auto-Reload Register (TIMx_ARR).
 * - Set the encoder interface mode counting on both TI1 and TI2 edges.
 * - Configure inputs.
 * - Enable counter.
 *
 * @param[in] timer_peripheral Timer register address base to configure.
 *
 * @see Reference manual (RM0008) "TIMx functional description" and in
 * particular "Encoder interface mode" section.
 */
static void configure_timer_as_quadrature_encoder(uint32_t timer_peripheral)
{
	timer_set_period(timer_peripheral, 0xFFFF);
	timer_slave_set_mode(timer_peripheral, 0x3);
	timer_ic_set_input(timer_peripheral, TIM_IC1, TIM_IC_IN_TI1);
	timer_ic_set_input(timer_peripheral, TIM_IC2, TIM_IC_IN_TI2);
	timer_enable_counter(timer_peripheral);
}


/**
 * @brief Setup timers for the motor encoders.
 *
 * TIM1 for the left motor and TIM4 for the right motor are configured.
 */
static void setup_encoders(void)
{
	configure_timer_as_quadrature_encoder(TIM1);
	configure_timer_as_quadrature_encoder(TIM4);
}


/**
 * @brief Set SysTick interruptions frequency and enable SysTick counter.
 *
 * SYSCLK is at 72 MHz as well as the Advanced High-permormance Bus (AHB)
 * because, by default, the AHB divider is set to 1, so the AHB clock has the
 * same frequency as the SYSCLK.
 *
 * @see STM32F103x8/STM32F103xB data sheet and in particular the "Clock tree"
 * figure.
 */
static void setup_systick(void)
{
	systick_set_frequency(1, 72000000);
	systick_counter_enable();
	systick_interrupt_enable();
}


/**
 * @brief Handle the SysTick interruptions.
 */
void sys_tick_handler(void)
{
	gpio_toggle(GPIOC, GPIO13);
}


/**
 * @brief Make `printf` send characters through the USART.
 */
int _write(int file, char *ptr, int len)
{
	int i;

	if (file == 1) {
		for (i = 0; i < len; i++)
			usart_send_blocking(USART3, ptr[i]);
		return i;
	}

	errno = EIO;
	return -1;
}


/**
 * @brief Set left motor power.
 *
 * Power is set modulating the PWM signal sent to the motor driver.
 *
 * @param[in] power Power value from 0 to 1000.
 */
static void power_left(uint32_t power)
{
	timer_set_oc_value(TIM3, TIM_OC3, power);
}


/**
 * @brief Set right motor power.
 *
 * Power is set modulating the PWM signal sent to the motor driver.
 *
 * @param[in] power Power value from 0 to 1000.
 */
static void power_right(uint32_t power)
{
	timer_set_oc_value(TIM3, TIM_OC4, power);
}


/**
 * @brief Set driving direction to forward in both motors.
 */
static void drive_forward(void)
{
	gpio_clear(GPIOB, GPIO13 | GPIO15);
	gpio_set(GPIOB, GPIO12 | GPIO14);
}


/**
 * @brief Set driving direction to backward in both motors.
 */
static void drive_backward(void)
{
	gpio_clear(GPIOB, GPIO12 | GPIO14);
	gpio_set(GPIOB, GPIO13 | GPIO15);
}


/**
 * @brief Break both motors.
 *
 * Set driver controlling signals to high to short break the driver outputs.
 * The break will be effective with any PWM input signal in the motor driver.
 */
static void drive_break(void)
{
	gpio_set(GPIOB, GPIO12 | GPIO13 | GPIO14 | GPIO15);
}


/**
 * @brief Read left motor encoder counter.
 */
static uint32_t read_encoder_left(void)
{
	return timer_get_counter(TIM1);
}


/**
 * @brief Read right motor encoder counter.
 */
static uint32_t read_encoder_right(void)
{
	return timer_get_counter(TIM4);
}

/**
 * @brief General timer setup
 *
 * TIM2 to read ADC1
 */
static void setup_timer(void)
{
	/* Set up the timer TIM2 for injected sampling */
	uint32_t timer;

	timer   = TIM2;
	rcc_periph_clock_enable(RCC_TIM2);

	/* Time Base configuration */
    rcc_periph_reset_pulse(RST_TIM2);
    timer_set_mode(timer, TIM_CR1_CKD_CK_INT,
	    TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_period(timer, 0xFF);
    timer_set_prescaler(timer, 0x8);
    timer_set_clock_division(timer, 0x0);
    /* Generate TRGO on every update. */
    timer_set_master_mode(timer, TIM_CR2_MMS_UPDATE);
    timer_enable_counter(timer);
}

/**
 * @brief Setup for adc: scan mode ADC1.
 */
static void setup_adc(void)
{
	int i;
	uint8_t channel_array[4]; 

	/* Make sure the ADC doesn't run during config. */
	adc_power_off(ADC1);

	/* We configure everything for one single timer triggered injected conversion with interrupt generation. */
	/* While not needed for a single channel, try out scan mode which does all channels in one sweep and
	 * generates the interrupt/EOC/JEOC flags set at the end of all channels, not each one.
	 */
	adc_enable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	/* We want to start the injected conversion with the TIM2 TRGO */
	adc_enable_external_trigger_injected(ADC1,ADC_CR2_JEXTSEL_TIM2_TRGO);
	/* Generate the ADC1_2_IRQ */
	adc_enable_eoc_interrupt_injected(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);

	/* Select the channels we want to convert.
	 * 3=Sensor1, 4=Sensor2, 5=Sensor3, 6=Sensor4
	 */
	channel_array[0] = 3;
	channel_array[1] = 4;
	channel_array[2] = 5;
	channel_array[3] = 6;
	adc_set_injected_sequence(ADC1, 4, channel_array);

	adc_power_on(ADC1);

	/* Wait for ADC starting up. */
	for (i = 0; i < 800000; i++)    /* Wait a bit. */
		__asm__("nop");

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

/**
 * @brief Interruptions setup
 */
static void setup_irq(void)
{
	/* Enable the adc1_2_isr() routine */
    nvic_set_priority(NVIC_ADC1_2_IRQ, 0);
    nvic_enable_irq(NVIC_ADC1_2_IRQ);
}

/**
 * @brief Initial setup and infinite wait.
 */
int main(void)
{
	setup_clock();
	setup_gpio();
	setup_usart();
	setup_encoders();
	setup_pwm();
	setup_systick();
	setup_timer();
	setup_irq();	
	setup_adc();

	drive_forward();

	while (1) {

		for (int i = 0; i < 8000; i++)
			__asm__("nop");
		printf("S1: %d, S2: %d, S3: %d, S4: %d\n", sensor_1, sensor_2,sensor_3,sensor_4);
	}

	return 0;
}

/**
 * @brief Interruption routine.
 */
void adc1_2_isr(void)
{
    /* Clear Injected End Of Conversion (JEOC) */
    ADC_SR(ADC1) &= ~ADC_SR_JEOC;
    sensor_1 = adc_read_injected(ADC1,1);
    sensor_2 = adc_read_injected(ADC1,2);
    sensor_3 = adc_read_injected(ADC1,3);
    sensor_4 = adc_read_injected(ADC1,4);
}
