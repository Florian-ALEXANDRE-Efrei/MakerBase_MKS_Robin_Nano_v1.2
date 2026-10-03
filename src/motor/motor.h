#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

// API minimale pour piloter des moteurs pas à pas via un driver STEP / DIR / ENABLE.
//
// Les impulsions STEP sont générées par le timer TIM6 (interruption à MOTOR_TICK_HZ) :
// motor_move() rend la main tout de suite et le mouvement se déroule en arrière-plan,
// avec une rampe d'accélération et de décélération (trapèze) pour ne pas perdre de pas.
//
// Moteur Z (carte MKS Robin Nano v1.2, mêmes broches que Marlin) :
//   ENABLE = PB8 (actif à l'état bas), STEP = PB5, DIR = PB4.
// PB4 est la broche NJTRST du JTAG : motor_init() désactive le JTAG (le SWD reste actif).
//
// Pour ajouter un moteur : ajouter une entrée dans l'enum ci-dessous ET dans la table
// motor_pins[] de motor.c.

typedef enum {
    MOTOR_Z = 0,
    MOTOR_COUNT
} motor_id_t;

// Codes de retour de motor_move()
#define MOTOR_OK            0
#define MOTOR_ERR_BUSY     -1   // un mouvement est déjà en cours sur ce moteur
#define MOTOR_ERR_DISABLED -2   // moteur non activé : appeler motor_enable(m, 1) d'abord
#define MOTOR_ERR_ARG      -3   // moteur inconnu

// Réglages (modifiables ici, ou avec -D dans platformio.ini)
#ifndef MOTOR_TICK_HZ
#define MOTOR_TICK_HZ     20000u  // fréquence de l'interruption ; vitesse max = MOTOR_TICK_HZ / 2
#endif
#ifndef MOTOR_MAX_SPEED
#define MOTOR_MAX_SPEED   10000u  // pas/s ; ne pas dépasser MOTOR_TICK_HZ / 2
#endif
#ifndef MOTOR_START_SPEED
#define MOTOR_START_SPEED   200u  // pas/s : vitesse de départ et d'arrivée de la rampe
#endif
#ifndef MOTOR_ACCEL
#define MOTOR_ACCEL        5000u  // pas/s² : accélération et décélération
#endif

// À appeler une seule fois, APRÈS HAL_Init() et SystemClock_Config() (le timer en dépend).
// Tous les moteurs sont désactivés (courant coupé) à la sortie.
void motor_init(void);

// Active (1) ou désactive (0) le driver. Désactiver pendant un mouvement l'arrête net.
void motor_enable(motor_id_t m, int enable);

// Lance un déplacement de |steps| pas à speed pas/s (plafonné entre MOTOR_START_SPEED et
// MOTOR_MAX_SPEED). steps > 0 : broche DIR à l'état haut ; steps < 0 : état bas.
// Non bloquant. Retourne MOTOR_OK ou un code MOTOR_ERR_*. steps == 0 ne fait rien.
int motor_move(motor_id_t m, int32_t steps, uint32_t speed);

// 1 si un mouvement est en cours, 0 sinon.
int motor_is_moving(motor_id_t m);

// Attend (bloquant) la fin du mouvement en cours.
void motor_wait(motor_id_t m);

// Arrêt immédiat, sans rampe de décélération (arrêt d'urgence).
void motor_stop(motor_id_t m);

// Position en pas depuis motor_init() (+1 par pas dans le sens steps > 0).
int32_t motor_position(motor_id_t m);

#endif
