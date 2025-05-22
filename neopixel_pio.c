/**
 * @file neopixel_pio.c
 * @brief Programa que recebe os valores da intensidade de som do microfone e, com base nisso, ajusta a intensidade dos LEDs do NeoPixel.
 * @author Alessandro Marvão
 * Matrícula: 20251RSE.MTC0019
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/timer.h"

// Biblioteca gerada pelo arquivo .pio durante compilação.
#include "ws2818b.pio.h"

// Definição do número de LEDs e pino.
#define LED_COUNT 25
#define LED_PIN 7

// Define o canal ADC2 para o microfone
#define MIC_CHANNEL 2
// Define o pino 28 para o microfone
#define MIC_PIN 28
// Define o valor da voltagem de referência do microfone (valor máximo)
#define MIC_VREF 3.3f
// Tempo de execução do temporizador
#define TIMER_MS 50

// Recebe o valor bruto do microfone
uint16_t value;
// Recebe o valor ajustado do microfone
float adjusted_value;

// Matriz de valores RGB a serem endereçados no LED
const uint matrix[5][5][3] = {
	{{0, 0, 0}, {0, 0, 0}, {255, 0, 0}, {0, 0, 0}, {0, 0, 0}},
	{{0, 0, 0}, {0, 0, 0}, {255, 0, 0}, {0, 0, 0}, {0, 0, 0}},
	{{0, 0, 0}, {0, 0, 0}, {255, 0, 0}, {0, 0, 0}, {0, 0, 0}},
	{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
	{{0, 0, 0}, {0, 0, 0}, {255, 0, 0}, {0, 0, 0}, {0, 0, 0}}};

bool alarm = false;

// Definição de pixel GRB

struct pixel_t
{
	uint8_t G, R, B; // Três valores de 8-bits compõem um pixel.
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t; // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.

// Declaração do buffer de pixels que formam a matriz.
npLED_t leds[LED_COUNT];

// Variáveis para uso da máquina PIO.
PIO np_pio;
uint sm;

/**
 * Inicializa a máquina PIO para controle da matriz de LEDs.
 */
void npInit(uint pin)
{

	// Cria programa PIO.
	uint offset = pio_add_program(pio0, &ws2818b_program);
	np_pio = pio0;

	// Toma posse de uma máquina PIO.
	sm = pio_claim_unused_sm(np_pio, false);
	if (sm < 0)
	{
		np_pio = pio1;
		sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
	}

	// Inicia programa na máquina PIO obtida.
	ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);

	// Limpa buffer de pixels.
	for (uint i = 0; i < LED_COUNT; ++i)
	{
		leds[i].R = 0;
		leds[i].G = 0;
		leds[i].B = 0;
	}
}

/**
 * Atribui uma cor RGB a um LED.
 */
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b)
{
	leds[index].R = r * 0.05f;
	leds[index].G = g * 0.05f;
	leds[index].B = b * 0.05f;
}

/**
 * Limpa o buffer de pixels.
 */
void npClear()
{
	for (uint i = 0; i < LED_COUNT; ++i)
		npSetLED(i, 0, 0, 0);
}

/**
 * Escreve os dados do buffer nos LEDs.
 */
void npWrite()
{
	// Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO.
	for (uint i = 0; i < LED_COUNT; ++i)
	{
		pio_sm_put_blocking(np_pio, sm, leds[i].G);
		pio_sm_put_blocking(np_pio, sm, leds[i].R);
		pio_sm_put_blocking(np_pio, sm, leds[i].B);
	}
	sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
}

// Modificado do github: https://github.com/BitDogLab/BitDogLab-C/tree/main/neopixel_pio
// Função para converter a posição do matriz para uma posição do vetor.
int getIndex(int x, int y)
{
	// Se a linha for par (0, 2, 4), percorremos da esquerda para a direita.
	// Se a linha for ímpar (1, 3), percorremos da direita para a esquerda.
	if (y % 2 == 0)
	{
		return 24 - (y * 5 + x); // Linha par (esquerda para direita).
	}
	else
	{
		return 24 - (y * 5 + (4 - x)); // Linha ímpar (direita para esquerda).
	}
}

// Inicializa todas as configurações iniciais
void init()
{
	stdio_init_all();

	// Inicializa o ADC
	adc_init();
	// Inicializa o pino 28 como ADC
	adc_gpio_init(MIC_PIN);
	// Seleciona o canal ADC2
	adc_select_input(MIC_CHANNEL);

	// Inicializa matriz de LEDs NeoPixel.
	npInit(LED_PIN);

	// Limpa quaisquer registros anteriores nos LEDs
	npClear();

	npWrite(); // Escreve os dados nos LEDs.
}

void turn_on_led()
{

	for (int linha = 0; linha < 5; linha++)
	{
		for (int coluna = 0; coluna < 5; coluna++)
		{
			int posicao = getIndex(linha, coluna);
			npSetLED(posicao, matrix[coluna][linha][0], matrix[coluna][linha][1], matrix[coluna][linha][2]);
		}
	}

	npWrite();
	sleep_ms(1000);
	npClear();
	npWrite();
	sleep_ms(1000);

	alarm = false;
}

// Verifica a intensidade do áudio recebida pelo microfone
bool get_microphone_callback(struct repeating_timer *t)
{
	value = adc_read();
	adjusted_value = (value * MIC_VREF) / 4095.0f;

	// Intensidade da voz humana em uma conversa baixa
	if (value > 2060)
	{
		printf("ADC Valor recebido: %d, tensão: %2f V\n", value, adjusted_value);

		alarm = true;
	}

	return true;
}

void core1_entry() {
	bool alarm_core1 = false;

	while(true) {
		printf("Core 1: Atingiu o limite de som definido. Acionando LED\n");
		alarm_core1 = multicore_fifo_pop_blocking();

		if (alarm_core1) {
			turn_on_led();
		}

		sleep_ms(50);
	}
}

int main()
{
	init();
	multicore_launch_core1(core1_entry);

	struct repeating_timer timer;

	add_repeating_timer_ms(TIMER_MS, get_microphone_callback, NULL, &timer);

	while (true)
	{
		multicore_fifo_push_blocking(alarm);
		tight_loop_contents();
	}
}
