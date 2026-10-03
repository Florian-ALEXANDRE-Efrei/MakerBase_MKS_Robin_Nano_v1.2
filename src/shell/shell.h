#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

// API minimale de sortie texte sur USART3 (PB10 = TX, PB11 = RX non utilisée)
// vers le CH340 -> /dev/ttyUSB0. PB10 est aussi reliée à la LED D7 : elle vacille
// à chaque émission, c'est voulu (indicateur de communication).

// À appeler une seule fois, APRÈS la configuration de l'horloge (le débit en dépend).
void shell_init(uint32_t baudrate);

// Comme printf(), sortie sur USART3. Bloquant. Retourne le nombre de caractères du
// message, ou -1 en cas d'erreur d'émission. Convertit "\n" en "\r\n".
// Ne pas appeler depuis une interruption. Le message est tronqué à SHELL_BUFFER_SIZE - 1.
// Pas de %f par défaut (newlib-nano) : afficher les flottants en entier + fraction.
int shell_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
