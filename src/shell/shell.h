#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

// API minimale d'entrée / sortie texte sur USART3 (PB10 = TX, PB11 = RX)
// vers le CH340 -> /dev/ttyUSB0. PB10 est aussi reliée à la LED D7 : elle vacille
// à chaque émission, c'est voulu (indicateur de communication).

// À appeler une seule fois, APRÈS la configuration de l'horloge (le débit en dépend).
void shell_init(uint32_t baudrate);

// Comme printf(), sortie sur USART3. Bloquant. Retourne le nombre de caractères du
// message, ou -1 en cas d'erreur d'émission. Convertit "\n" en "\r\n".
// Ne pas appeler depuis une interruption. Le message est tronqué à SHELL_BUFFER_SIZE - 1.
// Pas de %f par défaut (newlib-nano) : afficher les flottants en entier + fraction.
int shell_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// --- Commandes saisies depuis /dev/ttyUSB0 (ex. picocom -b 115200 /dev/ttyUSB0) ---
//
// Une commande est une fonction recevant la ligne découpée en mots, comme main() :
//   static int cmd_hello(int argc, char **argv) { shell_printf("salut\n"); return 0; }
//   shell_register("hello", "dit bonjour", cmd_hello);
// argv[0] est le nom de la commande. Retourner 0 si tout va bien ; toute autre valeur
// affiche "erreur: <valeur>". La commande "help" est ajoutée automatiquement.
// Elle s'exécute dans le contexte de shell_poll() (pas dans une interruption) ;
// éviter les attentes longues, qui gèleraient le shell.
typedef int (*shell_cmd_fn)(int argc, char **argv);

// Enregistre une commande (name et help doivent rester valides : chaînes littérales).
// À faire avant la première boucle de shell_poll(). Retourne 0, ou -1 si la table est pleine.
int shell_register(const char *name, const char *help, shell_cmd_fn fn);

// À appeler en boucle depuis main() : traite les caractères reçus (écho, édition avec
// backspace) et exécute la commande à chaque fin de ligne. Non bloquant.
void shell_poll(void);

#endif
