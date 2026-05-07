// =============================================================================
// ELEC1620 Unit 4 Assessment - Washing Machine Control Panel
// =============================================================================
//
// PIN ASSIGNMENTS:
// -----------------------------------------------------------------------------
// OUTPUTS:
//   PC_0  -> LED1 (Green)  - Power ON indicator
//   PC_1  -> LED2 (Green)  - Cycle mode indicator A
//   PB_0  -> LED3 (Green)  - Cycle mode indicator B
//   PA_4  -> LED4 (Red)    - Running / Error indicator
//   PB_3  -> RGB Red       - Wash phase indicator
//   PB_4  -> RGB Blue      - Wash phase indicator
//   PB_5  -> RGB Green     - Wash phase indicator
//   PA_15 -> Buzzer        - Cycle complete / error alert
//   PB_1, PB_2, PB_11, PB_12, PA_11, PA_12, PB_14, PB_15 -> 7-Segment Display
//
// INPUTS:
//   PC_10 -> Button1       - Power ON/OFF toggle (ACTIVE LOW - pressed = 0)
//   PC_11 -> Button2       - Wash cycle selection (ACTIVE LOW - pressed = 0)
//   PD_2  -> Button3       - Start / Run cycle (ACTIVE LOW - pressed = 0)
//   PA_5  -> Pot1          - Simulated water temperature (ADC_CHANNEL_10)
//   PA_6  -> Pot2          - Spin speed selection (ADC_CHANNEL_11)
//   PA_7  -> Pot3          - Load size selection (ADC_CHANNEL_12)
//   PC_3  -> Temp Sensor   - Real ambient temperature (ADC_CHANNEL_4)
//   PA_1  -> FSR           - Door sensor (ADC_CHANNEL_6)
//
// =============================================================================
// FEATURES:
// -----------------------------------------------------------------------------
// Required Functions:
//   1) Power ON/OFF        - Button1 toggles power, LED1 indicates state
//   2) Cycle Selection     - Button2 cycles: Quick -> Heavy -> Delicate
//                            LED2/LED3 indicate selected mode
//   3) Parameter Check     - Pot1 simulates water temp (min 30 deg C)
//                            FSR checks door is closed before cycle runs
//   4) Run Function        - Button3 starts cycle, LED4 flashes during run
//                            Buzzer melody plays on completion
//   5) Status Interface    - Serial output shows all parameters in real time
//
// Additional Features:
//   + 7-Segment Display    - Shows cycle number when idle, countdown when running
//   + FSR Door Sensor      - Cycle blocked if door not closed
//   + RGB LED Phase        - Blue=Washing, Red=Rinsing, Green=Spinning
//   + Pot2 Spin Speed      - Low/Medium/High spin speed
//   + Pot3 Load Size       - Small/Medium/Large load (affects duration)
//   + Real Temp Sensor     - Ambient temperature displayed on serial
//   + Buzzer Melody        - Different tones for complete vs error
// =============================================================================

#include "main.h"
#include "adc.h"
#include "usart.h"
#include "gpio.h"
#include <stdio.h>
#include <string.h>

// =============================================================================
// Function Prototypes
// =============================================================================
void SystemClock_Config(void);
uint32_t read_adc_channel(uint32_t channel);
void Error_Handler(void);
void power_on_sequence(char *msg);
void power_off_sequence(char *msg);
void update_cycle_leds(int cycle_mode);
void run_wash_cycle(int cycle_mode, int spin_speed, int load_size, char *msg);
void seven_seg_display(int digit);
void seven_seg_off(void);
void set_rgb_phase(int phase);
void buzzer_complete_melody(void);
void buzzer_error_tone(void);

// =============================================================================
// Definitions
// =============================================================================
#define CYCLE_QUICK         0
#define CYCLE_HEAVY         1
#define CYCLE_DELICATE      2

#define QUICK_DURATION      15000   // 15 seconds (demo)
#define HEAVY_DURATION      25000   // 25 seconds (demo)
#define DELICATE_DURATION   20000   // 20 seconds (demo)

#define TEMP_MIN_C          30      // Minimum water temperature in deg C
#define FSR_DOOR_THRESHOLD  500     // FSR ADC threshold - below = door open
#define DEBOUNCE_MS         200     // Button debounce delay

// =============================================================================
// Main
// =============================================================================
int main(void)
{
    // Initialise HAL and peripherals
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_ADC1_Init();

    // Serial message buffer
    char msg[300];

    // Startup message
    sprintf(msg, "\r\n=== ELEC1620 Washing Machine Control Panel ===\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
    sprintf(msg, "System ready. Press Button1 to power ON.\r\n\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);

    // Ensure all outputs are OFF at startup
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
    set_rgb_phase(0);
    seven_seg_off();

    // Auto power on for testing
    power_on_sequence(msg);
    seven_seg_display(1);update_cycle_leds(CYCLE_QUICK);

    // State variables
    int power_state     = 1;
    int cycle_mode      = CYCLE_QUICK;
    int machine_running = 0;

    // Previous button states for edge detection
    // Buttons are ACTIVE LOW: unpressed = 1, pressed = 0
    int btn1_prev = 1;
    int btn2_prev = 1;
    int btn3_prev = 1;

    // =========================================================================
    // Main Control Loop
    // =========================================================================
    while (1)
    {
        // Read button states
        // Buttons use internal pull-ups: unpressed = GPIO_PIN_SET (1)
        //                                pressed   = GPIO_PIN_RESET (0)
        int btn1 = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_10); // Power
        int btn2 = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11); // Cycle select
        int btn3 = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_2);  // Start/Run

        // Read analogue inputs
        uint32_t pot1_raw  = read_adc_channel(ADC_CHANNEL_10); // Simulated water temp
        uint32_t pot2_raw  = read_adc_channel(ADC_CHANNEL_11); // Spin speed
        uint32_t pot3_raw  = read_adc_channel(ADC_CHANNEL_12); // Load size
        uint32_t real_temp = read_adc_channel(ADC_CHANNEL_4);  // Real temp sensor
        uint32_t fsr_val   = read_adc_channel(ADC_CHANNEL_6);  // FSR door sensor

        // Convert pot1 to simulated temperature (0-100 deg C)
        uint32_t sim_temp_c = (pot1_raw * 100) / 4095;

        // Convert real temp sensor to deg C (LM35: 10mV/degC, 3.3V/12-bit)
        uint32_t real_temp_c = (real_temp * 330) / 4095;

        // Spin speed from pot2 (0=Low, 1=Medium, 2=High)
        int spin_speed;
        if (pot2_raw < 1366)      spin_speed = 0;
        else if (pot2_raw < 2731) spin_speed = 1;
        else                      spin_speed = 2;

        // Load size from pot3 (0=Small, 1=Medium, 2=Large)
        int load_size;
        if (pot3_raw < 1366)      load_size = 0;
        else if (pot3_raw < 2731) load_size = 1;
        else                      load_size = 2;

        // String arrays for display
        const char *cycle_names[] = {"QUICK", "HEAVY", "DELICATE"};
        const char *spin_names[]  = {"Low", "Medium", "High"};
        const char *load_names[]  = {"Small", "Medium", "Large"};

        // =====================================================================
        // FUNCTION 1: POWER ON/OFF
        // Button1 toggles machine power (ACTIVE LOW: pressed = 0)
        // Detect falling edge: was 1 (unpressed), now 0 (pressed)
        // LED1 (PC_0) lights green when ON.
        // =====================================================================
        if (btn1 == 0 && btn1_prev == 1)
        {
            HAL_Delay(DEBOUNCE_MS);
            power_state = !power_state;

            if (power_state == 1)
            {
                power_on_sequence(msg);
                seven_seg_display(cycle_mode + 1);
                update_cycle_leds(cycle_mode);
            }
            else
            {
                power_off_sequence(msg);
                cycle_mode = CYCLE_QUICK;
            }
        }
        btn1_prev = btn1;

        // =====================================================================
        // Functions 2-5 only active when powered ON
        // =====================================================================
        if (power_state == 1)
        {
            // =================================================================
            // FUNCTION 2: WASH CYCLE SELECTION
            // Button2 cycles through Quick -> Heavy -> Delicate (ACTIVE LOW)
            // LED2/LED3 pattern indicates selected cycle.
            // 7-segment shows cycle number (1, 2, or 3).
            // =================================================================
            if (btn2 == 0 && btn2_prev == 1 && machine_running == 0)
            {
                HAL_Delay(DEBOUNCE_MS);
                cycle_mode = (cycle_mode + 1) % 3;
                update_cycle_leds(cycle_mode);
                seven_seg_display(cycle_mode + 1);

                sprintf(msg, "[CYCLE] Selected: %s WASH\r\n", cycle_names[cycle_mode]);
                HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
            }
            btn2_prev = btn2;

            // =================================================================
            // FUNCTION 3 & 4: PARAMETER CHECK + RUN
            // Button3 triggers parameter checks before starting cycle (ACTIVE LOW)
            // Checks: water temperature >= 30 deg C, door closed (FSR).
            // If checks pass: runs cycle with RGB phases and countdown.
            // If checks fail: error LED flash + error buzzer tone.
            // =================================================================
            if (btn3 == 0 && btn3_prev == 1 && machine_running == 0)
            {
                HAL_Delay(DEBOUNCE_MS);

                int params_ok = 1;

                // Check 1: Water temperature (Pot1 simulates water temp)
                if (sim_temp_c < TEMP_MIN_C)
                {
                    params_ok = 0;
                    sprintf(msg, "[ERROR] Water temp too low: %lu degC (min %d degC).\r\n",
                            sim_temp_c, TEMP_MIN_C);
                    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
                }

                // Check 2: Door sensor (FSR must be pressed = door closed)
                if (fsr_val < FSR_DOOR_THRESHOLD)
                {
                    params_ok = 0;
                    sprintf(msg, "[ERROR] Door open! Close door before starting. FSR=%lu\r\n",
                            fsr_val);
                    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
                }

                if (params_ok == 0)
                {
                    // Error: flash LED4 and play error tone
                    for (int i = 0; i < 3; i++)
                    {
                        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
                        HAL_Delay(100);
                        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
                        HAL_Delay(100);
                    }
                    buzzer_error_tone();
                }
                else
                {
                    // All checks passed - start cycle
                    machine_running = 1;
                    sprintf(msg, "\r\n[RUN] Checks passed. Starting %s...\r\n",
                            cycle_names[cycle_mode]);
                    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);

                    run_wash_cycle(cycle_mode, spin_speed, load_size, msg);

                    machine_running = 0;

                    // Notify user cycle is complete
                    sprintf(msg, "[COMPLETE] Wash done! Remove laundry.\r\n\r\n");
                    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);

                    buzzer_complete_melody();

                    // Reset outputs after cycle
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
                    set_rgb_phase(0);
                    seven_seg_display(cycle_mode + 1);
                }
            }
            btn3_prev = btn3;

            // =================================================================
            // FUNCTION 5: STATUS INTERFACE
            // Continuously prints machine status to serial terminal.
            // =================================================================
            sprintf(msg,
                "[STATUS] ON | Cycle:%s | WaterTemp:%lu C | RealTemp:%lu C | "
                "Door:%s | Spin:%s | Load:%s | Running:%s\r\n",
                cycle_names[cycle_mode],
                sim_temp_c,
                real_temp_c,
                fsr_val >= FSR_DOOR_THRESHOLD ? "CLOSED" : "OPEN",
                spin_names[spin_speed],
                load_names[load_size],
                machine_running ? "YES" : "NO");
            HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
        }
        else
        {
            sprintf(msg, "[STATUS] Power:OFF\r\n");
            HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
        }

        HAL_Delay(300);
    }
}

// =============================================================================
// POWER ON SEQUENCE
// =============================================================================
void power_on_sequence(char *msg)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET); // LED1 ON
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
    sprintf(msg, "[POWER] Machine ON. Welcome!\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

// =============================================================================
// POWER OFF SEQUENCE
// =============================================================================
void power_off_sequence(char *msg)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
    set_rgb_phase(0);
    seven_seg_off();
    sprintf(msg, "[POWER] Machine OFF. Goodbye!\r\n\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

// =============================================================================
// UPDATE CYCLE LEDs
// Quick: LED2=ON LED3=OFF | Heavy: LED2=OFF LED3=ON | Delicate: both ON
// =============================================================================
void update_cycle_leds(int cycle_mode)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1,
        (cycle_mode == CYCLE_QUICK || cycle_mode == CYCLE_DELICATE) ?
        GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,
        (cycle_mode == CYCLE_HEAVY || cycle_mode == CYCLE_DELICATE) ?
        GPIO_PIN_SET : GPIO_PIN_RESET);
}

// =============================================================================
// RUN WASH CYCLE
// =============================================================================
void run_wash_cycle(int cycle_mode, int spin_speed, int load_size, char *msg)
{
    uint32_t duration;
    const char *cycle_name;
    const char *spin_names[] = {"Low", "Medium", "High"};
    const char *load_names[] = {"Small", "Medium", "Large"};

    if (cycle_mode == CYCLE_QUICK)
    {
        duration = QUICK_DURATION;
        cycle_name = "QUICK WASH";
    }
    else if (cycle_mode == CYCLE_HEAVY)
    {
        duration = HEAVY_DURATION;
        cycle_name = "HEAVY WASH";
    }
    else
    {
        duration = DELICATE_DURATION;
        cycle_name = "DELICATE WASH";
    }

    // Adjust duration based on load size
    if (load_size == 1) duration = (duration * 120) / 100;
    if (load_size == 2) duration = (duration * 140) / 100;

    sprintf(msg, "[RUN] %s | Spin:%s | Load:%s | Duration:%lus\r\n",
            cycle_name, spin_names[spin_speed], load_names[load_size],
            duration / 1000);
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);

    uint32_t start_time = HAL_GetTick();
    uint32_t elapsed    = 0;
    uint32_t last_print = 0;

    while (elapsed < duration)
    {
        elapsed = HAL_GetTick() - start_time;

        int progress       = (elapsed * 100) / duration;
        int remaining_secs = (int)((duration - elapsed) / 1000);

        // 7-segment countdown
        seven_seg_display(remaining_secs % 10);

        // LED4 toggle to show running
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_4);

        // RGB phase based on progress
        if (progress < 34)      set_rgb_phase(1); // Blue = Washing
        else if (progress < 67) set_rgb_phase(2); // Red = Rinsing
        else                    set_rgb_phase(3); // Green = Spinning

        // Serial progress every 2 seconds
        if (elapsed - last_print >= 2000)
        {
            last_print = elapsed;
            const char *phase = progress < 34 ? "Washing" :
                                progress < 67 ? "Rinsing" : "Spinning";
            sprintf(msg, "[RUN] %s | Phase:%s | Progress:%d%% | Remaining:%ds\r\n",
                    cycle_name, phase, progress, remaining_secs);
            HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
        }

        HAL_Delay(500);
    }

    set_rgb_phase(0);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    seven_seg_display(0);
}

// =============================================================================
// 7-SEGMENT DISPLAY
// =============================================================================
void seven_seg_display(int digit)
{
    const int p[10][7] = {
        {1,1,1,1,1,1,0}, // 0
        {0,1,1,0,0,0,0}, // 1
        {1,1,0,1,1,0,1}, // 2
        {1,1,1,1,0,0,1}, // 3
        {0,1,1,0,0,1,1}, // 4
        {1,0,1,1,0,1,1}, // 5
        {1,0,1,1,1,1,1}, // 6
        {1,1,1,0,0,0,0}, // 7
        {1,1,1,1,1,1,1}, // 8
        {1,1,1,1,0,1,1}, // 9
    };

    if (digit < 0 || digit > 9) digit = 0;

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1,  p[digit][0] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2,  p[digit][1] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, p[digit][2] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, p[digit][3] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, p[digit][4] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, p[digit][5] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, p[digit][6] ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// =============================================================================
// Turn off all 7-segment segments
// =============================================================================
void seven_seg_off(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);
}

// =============================================================================
// RGB LED PHASE INDICATOR
// PB3=Red, PB4=Blue, PB5=Green
// Phase: 0=off, 1=Blue(Washing), 2=Red(Rinsing), 3=Green(Spinning)
// =============================================================================
void set_rgb_phase(int phase)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);

    if      (phase == 1) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET); // Blue
    else if (phase == 2) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET); // Red
    else if (phase == 3) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET); // Green
}

// =============================================================================
// BUZZER COMPLETE MELODY
// =============================================================================
void buzzer_complete_melody(void)
{
    int on_times[]  = {80, 60, 40, 200};
    int off_times[] = {80, 60, 40, 0};
    for (int i = 0; i < 4; i++)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
        HAL_Delay(on_times[i]);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
        HAL_Delay(off_times[i]);
    }
}

// =============================================================================
// BUZZER ERROR TONE
// =============================================================================
void buzzer_error_tone(void)
{
    for (int i = 0; i < 2; i++)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
        HAL_Delay(400);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
        HAL_Delay(200);
    }
}

// =============================================================================
// read_adc_channel() - DO NOT MODIFY
// =============================================================================
uint32_t read_adc_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    uint32_t value = 0;

    sConfig.Channel      = channel;
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
    sConfig.SingleDiff   = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset       = 0;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
        value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    return value;
}

// =============================================================================
// SystemClock_Config() - DO NOT MODIFY
// =============================================================================
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM           = 1;
    RCC_OscInitStruct.PLL.PLLN           = 10;
    RCC_OscInitStruct.PLL.PLLP           = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ           = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR           = RCC_PLLR_DIV2;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
        Error_Handler();
}

// =============================================================================
// Error_Handler() - DO NOT MODIFY
// =============================================================================
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}