/* dcr_config.h -- the user's settings, from <game folder>/config.ini (dcr_config.c). */
#ifndef DCR_USER_CONFIG_H
#define DCR_USER_CONFIG_H

typedef struct {
  int pointer;         /* [controls] stick_pointer */
  float pointer_speed; /* [controls] pointer_speed */
  int touch;           /* [touch] enabled */
  int res_w, res_h;    /* [display] resolution */
  int boost;           /* [performance] boost_cpu_when_loading */
  int cpu_cores;       /* [performance] cpu_cores */
  int gl_selftest;     /* [debug] gl_selftest */
  int boot_log;        /* [debug] boot_log_on_screen */
  int log_jni;         /* [debug] log_java_calls */
  int log_input;       /* [debug] log_input */
  int log_commands;    /* [debug] log_commands */
  char language[12];   /* [game] language: "auto", or a code ("en", "de") */
  int tv_mode;         /* [game] tv_mode: the game is told it runs on a television */
  int gyro;            /* [controls] gyro_pointer: on when the game starts */
  float gyro_speed;    /* [controls] gyro_speed */
  int gyro_invert_x;   /* [controls] gyro_invert_x */
  int gyro_invert_y;   /* [controls] gyro_invert_y */
  int pointer_style;   /* [controls] pointer_style: 0 cross, 1 dot, 2 circle */
  int gyro_space;      /* [controls] gyro_space: 0 world, 1 local */
} DcrConfig;

/* Read config.ini (writing it with the defaults, or adding missing options,
 * first). Early in main(); the defaults hold until then. */
void dcr_config_load(void);
const DcrConfig *dcr_config(void);

#endif
