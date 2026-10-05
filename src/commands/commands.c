#include "commands.h"

#include <stdlib.h>
#include "stm32f1xx_hal.h"
#include "shell/shell.h"
#include "motor/motor.h"

// Convertit un argument en entier ; retourne 0 si valide, -1 sinon
static int parse_long(const char *s, long *out)
{
    char *end;
    *out = strtol(s, &end, 0);
    return (end != s && *end == '\0') ? 0 : -1;
}

static int cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_printf("%lu ms\n", (unsigned long)HAL_GetTick());
    return 0;
}

// enable <0|1> : moteur Z
static int cmd_enable(int argc, char **argv)
{
    long v;
    if (argc != 2 || parse_long(argv[1], &v) < 0 || (v != 0 && v != 1))
    {
        shell_printf("usage: enable <0|1>\n");
        return -1;
    }
    motor_enable(MOTOR_Z, (int)v);
    return 0;
}

// move <pas> [vitesse] : moteur Z, non bloquant
static int cmd_move(int argc, char **argv)
{
    long steps, speed = 800;
    if ((argc != 2 && argc != 3) || parse_long(argv[1], &steps) < 0 ||
        (argc == 3 && (parse_long(argv[2], &speed) < 0 || speed <= 0)))
    {
        shell_printf("usage: move <pas> [pas/s]\n");
        return -1;
    }
    int ret = motor_move(MOTOR_Z, (int32_t)steps, (uint32_t)speed);
    if (ret == MOTOR_ERR_BUSY)
        shell_printf("mouvement deja en cours\n");
    else if (ret == MOTOR_ERR_DISABLED)
        shell_printf("moteur desactive : 'enable 1' d'abord\n");
    return ret;
}

static int cmd_stop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    motor_stop(MOTOR_Z);
    return 0;
}

static int cmd_pos(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_printf("position = %ld%s\n", (long)motor_position(MOTOR_Z),
                 motor_is_moving(MOTOR_Z) ? " (en mouvement)" : "");
    return 0;
}

void commands_init(void)
{
    shell_register("uptime", "temps depuis le demarrage", cmd_uptime);
    shell_register("enable", "enable <0|1> : active le moteur Z", cmd_enable);
    shell_register("move", "move <pas> [pas/s] : deplace Z (<0 = autre sens)", cmd_move);
    shell_register("stop", "arret immediat du moteur Z", cmd_stop);
    shell_register("pos", "position du moteur Z", cmd_pos);
}
