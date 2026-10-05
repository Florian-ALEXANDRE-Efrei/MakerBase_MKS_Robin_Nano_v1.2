#ifndef COMMANDS_H
#define COMMANDS_H

// Enregistre les commandes shell de l'application (uptime, enable, move, stop, pos)
// auprès de shell_register(). À appeler une seule fois, après shell_init().
void commands_init(void);

#endif
