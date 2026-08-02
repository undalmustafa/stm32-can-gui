#include "boot_jump.h"

#include <stddef.h>

#include "stm32h7xx.h"

typedef struct
{
    Boot_JumpQuiesceFn quiesce;
    void *quiesce_context;
} Boot_JumpStm32Context_t;

static uint8_t Boot_Jump_Stm32Quiesce(void *context)
{
    Boot_JumpStm32Context_t *stm32_context =
        (Boot_JumpStm32Context_t *)context;

    return stm32_context->quiesce(stm32_context->quiesce_context);
}

static void Boot_Jump_Stm32MaskInterrupts(void *context)
{
    (void)context;
    __disable_irq();
}

static void Boot_Jump_Stm32StopSysTick(void *context)
{
    (void)context;
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
}

static void Boot_Jump_Stm32ClearInterruptState(void *context)
{
    uint32_t register_index;

    (void)context;
    for (register_index = 0U;
         register_index < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]));
         register_index++)
    {
        NVIC->ICER[register_index] = UINT32_MAX;
        NVIC->ICPR[register_index] = UINT32_MAX;
    }

    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    __DSB();
    __ISB();
}

static void Boot_Jump_Stm32DisableCaches(void *context)
{
    (void)context;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_DisableDCache();
    }
#endif
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U)
    {
        SCB_DisableICache();
    }
#endif
    __DSB();
    __ISB();
}

static void Boot_Jump_Stm32SetVector(void *context,
                                    uint32_t vector_address)
{
    (void)context;
    SCB->VTOR = vector_address;
    __DSB();
    __ISB();
}

__attribute__((noreturn))
static void Boot_Jump_Stm32Branch(void *context,
                                 uint32_t initial_stack_pointer,
                                 uint32_t entry_address)
{
    uint32_t reset_value = 0U;

    (void)context;
    __asm volatile(
        "msr control, %[reset]\n"
        "msr psp, %[reset]\n"
        "msr basepri, %[reset]\n"
        "msr faultmask, %[reset]\n"
        "dsb\n"
        "isb\n"
        "msr msp, %[stack]\n"
        "cpsie i\n"
        "bx %[entry]\n"
        :
        : [reset] "r" (reset_value),
          [stack] "r" (initial_stack_pointer),
          [entry] "r" (entry_address)
        : "memory");
    __builtin_unreachable();
}

__attribute__((noreturn))
static void Boot_Jump_Stm32Halt(void *context)
{
    (void)context;
    __disable_irq();
    for (;;)
    {
        __WFI();
    }
}

Boot_JumpResult_t Boot_Jump_Stm32(
    const Boot_JumpPlan_t *plan,
    Boot_JumpQuiesceFn quiesce_peripherals,
    void *context)
{
    Boot_JumpStm32Context_t stm32_context;
    Boot_JumpOperations_t operations;

    if (quiesce_peripherals == NULL)
    {
        return BOOT_JUMP_RESULT_INVALID_ARGUMENT;
    }

    stm32_context.quiesce = quiesce_peripherals;
    stm32_context.quiesce_context = context;
    operations.quiesce_peripherals = Boot_Jump_Stm32Quiesce;
    operations.mask_interrupts = Boot_Jump_Stm32MaskInterrupts;
    operations.stop_systick = Boot_Jump_Stm32StopSysTick;
    operations.clear_interrupt_state = Boot_Jump_Stm32ClearInterruptState;
    operations.disable_caches = Boot_Jump_Stm32DisableCaches;
    operations.set_vector_table = Boot_Jump_Stm32SetVector;
    operations.branch = Boot_Jump_Stm32Branch;
    operations.halt = Boot_Jump_Stm32Halt;
    operations.context = &stm32_context;
    return Boot_Jump_Execute(plan, &operations);
}
