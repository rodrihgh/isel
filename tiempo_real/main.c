/*

PRACTICA 1 DE ISEL CURSO 2014/2015
Autores: Rodrigo Hernangomez Herrero y Ana Jimenez Valbuena
Febrero 2015

*/


#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <wiringPi.h>
#include "fsm.h"
#include <stdio.h>

#define GPIO_BUTTON	2
#define GPIO_LED	3
#define GPIO_CUP	4
#define GPIO_COFFEE	5
#define GPIO_MILK	6
#define GPIO_MONEY0   7 // Los tres pines determinan el tipo de moneda que se ha introducido
#define GPIO_MONEY1   8
#define GPIO_MONEY2   9
#define GPIO_COIN     10  // Flag que interrumpe avisandonos de que hay moneda
#define GPIO_5C       11  // Cada pin devuelve un tipo de moneda al activarse. 
#define GPIO_10C      12  // Se supone que el mecanismo para devolver monedas se activa por flanco
#define GPIO_20C      13
#define GPIO_50C      14
#define GPIO_1E       15
#define GPIO_2E       16

#define CUP_TIME	250
#define COFFEE_TIME	3000
#define MILK_TIME	3000

#define PRECIO         50 // Expresado en centimos para poder trabajar con enteros


static int dinero = 0; // Variable global que lleva la cuenta del dinero

static int cobrar = 0; // Flag que indica al monedero que debemos cobrar:                         |<--|cobrar
                                                                                      //MONEDERO  |   |   MAQUINA DE CAFE
static int hay_dinero = 0;  // Flag con la que el monedero nos avisa de si hay dinero   hay_dinero|-->|

enum cofm_state {
  COFM_WAITING,
  COFM_CUP,
  COFM_COFFEE,
  COFM_MILK,
};

enum cashm_state {
  COFM_MONEY,
  COFM_VUELTAS,
};

enum monedas{   //Tipo de monedas que acepta la máquina
  C5=1,
  C10=2,
  C20=3,
  C50=4,
  E1=5,
  E2=6,
};

static int button = 0;
static void button_isr (void) { button = 1; }

static int money = 0;
static void money_isr (void) { 
  int monedaID = digitalRead(GPIO_MONEY2)*4+digitalRead(GPIO_MONEY1)*2+digitalRead(GPIO_MONEY0); // Combinamos tres entradas en un numero de 3 bits
  int valor;
  switch(monedaID){
    case C5 :
      valor=5;
      break;
    case C10 :
      valor=10;
      break;
    case C20 :
      valor=20;
      break;
    case C50 :
      valor=50;
      break;
    case E1 :
      valor=100;
      break;
    case E2 :
      valor=200;
      break;
    default :
      valor=0;
      break;
  }

  dinero+=valor;
  if(dinero>=PRECIO) money=1;
}    //INTERRUPCIÓN PARA LA ENTRADA DE MONEDAS


static int timer = 0;
static void timer_isr (union sigval arg) { timer = 1; }
static void timer_start (int ms)
{
  timer_t timerid;
  struct itimerspec value;
  struct sigevent se;
  se.sigev_notify = SIGEV_THREAD;
  se.sigev_value.sival_ptr = &timerid;
  se.sigev_notify_function = timer_isr;
  se.sigev_notify_attributes = NULL;
  value.it_value.tv_sec = ms / 1000;
  value.it_value.tv_nsec = (ms % 1000) * 1000000;
  value.it_interval.tv_sec = 0;
  value.it_interval.tv_nsec = 0;
  timer_create (CLOCK_REALTIME, &se, &timerid);
  timer_settime (timerid, 0, &value, NULL);
}

static int button_pressed (fsm_t* this)
{
  int ret1 = button;
  int ret2 = hay_dinero;
  if(ret1&&ret2){         // Comprueba que ambos se cumplen antes de resetearlos
    button  = 0;
    hay_dinero = 0;
    return 1;
  } else return 0;
}

static int insert_coin (fsm_t* this)
{
  int ret = money;
  money = 0;
  return ret;
}

static int coffee_served (fsm_t* this)
{
  return cobrar; // En este caso es la funcion de salida getChange la que resetea el flag de cobrar
}

static int timer_finished (fsm_t* this)
{
  int ret = timer;
  timer = 0;
  return ret;
}

static void cup (fsm_t* this)
{
  digitalWrite (GPIO_LED, LOW);
  digitalWrite (GPIO_CUP, HIGH);
  timer_start (CUP_TIME);
}

static void coffee (fsm_t* this)
{
  digitalWrite (GPIO_CUP, LOW);
  digitalWrite (GPIO_COFFEE, HIGH);
  timer_start (COFFEE_TIME);
}

static void milk (fsm_t* this)
{
  digitalWrite (GPIO_COFFEE, LOW);
  digitalWrite (GPIO_MILK, HIGH);
  timer_start (MILK_TIME);
}

static void finish (fsm_t* this)
{
  digitalWrite (GPIO_MILK, LOW);
  digitalWrite (GPIO_LED, HIGH);
  cobrar = 1;
  dinero -= PRECIO;
}

static void enough_money(fsm_t* this)
{
  hay_dinero = 1;
}

static void getChange(fsm_t* this)
{ 
  // Estructura de control que va devolviendo el cambio. Cada "palanca devuelve-cambio" se activa por flanco
  // Quiza sea mas elegante usar un bucle y una estructura de arrays, pero esta forma es mas entendible
  if(dinero>=200){
    dinero-=200;
    digitalWrite(GPIO_2E, HIGH);
    digitalWrite(GPIO_2E, LOW);
  }else if(dinero>=100){
    dinero-=100;
    digitalWrite(GPIO_1E, HIGH);
    digitalWrite(GPIO_1E, LOW);
  }else if(dinero>=50){
    dinero-=50;
    digitalWrite(GPIO_50C, HIGH);
    digitalWrite(GPIO_50C, LOW);
  }else if(dinero>=20){
    dinero-=20;
    digitalWrite(GPIO_20C, HIGH);
    digitalWrite(GPIO_20C, LOW);
  }else if(dinero>=10){
    dinero-=10;
    digitalWrite(GPIO_10C, HIGH);
    digitalWrite(GPIO_10C, LOW);
  }else if(dinero>=5){
    dinero-=5;
    digitalWrite(GPIO_5C, HIGH);
    digitalWrite(GPIO_5C, LOW);
  }
  
  if(dinero>=0) this->current_state=COFM_VUELTAS;
  else cobrar=0;
}


// Explicit FSM description
static fsm_trans_t cofm[] = {
  { COFM_WAITING, button_pressed, COFM_CUP,     cup    },
  { COFM_CUP,     timer_finished, COFM_COFFEE,  coffee },
  { COFM_COFFEE,  timer_finished, COFM_MILK,    milk   },
  { COFM_MILK,    timer_finished, COFM_WAITING, finish },
  {-1, NULL, -1, NULL },
};

static fsm_trans_t cashm[] = {
  { COFM_MONEY, insert_coin, COFM_VUELTAS, enough_money},
  { COFM_VUELTAS, coffee_served, COFM_MONEY, getChange},   //DEVUELVE EL DINERO CUANDO HA ACABADO DE ECHAR LA LECHE
  {-1, NULL, -1, NULL },
};




// Utility functions, should be elsewhere

// res = a - b
void
timeval_sub (struct timeval *res, struct timeval *a, struct timeval *b)
{
  res->tv_sec = a->tv_sec - b->tv_sec;
  res->tv_usec = a->tv_usec - b->tv_usec;
  if (res->tv_usec < 0) {
    --res->tv_sec;
    res->tv_usec += 1000000;
  }
}

// res = a + b
void
timeval_add (struct timeval *res, struct timeval *a, struct timeval *b)
{
  res->tv_sec = a->tv_sec + b->tv_sec
    + a->tv_usec / 1000000 + b->tv_usec / 1000000; 
  res->tv_usec = a->tv_usec % 1000000 + b->tv_usec % 1000000;
}

// res = a - b
void
timespec_sub (struct timespec *res, struct timespec *a, struct timespec *b)
{
  res->tv_sec = a->tv_sec - b->tv_sec;
  res->tv_nsec = a->tv_nsec - b->tv_nsec;
  if (res->tv_nsec < 0) {
    --res->tv_sec;
    res->tv_nsec += 1000000000;
  }
}

// res = a + b
void
timespec_add (struct timespec *res, struct timespec *a, struct timespec *b)
{
  res->tv_sec = a->tv_sec + b->tv_sec
    + a->tv_nsec / 1000000000 + b->tv_nsec / 1000000000; 
  res->tv_nsec = a->tv_nsec % 1000000000 + b->tv_nsec % 1000000000;
}

// max = max{a,b}
void
timespec_max (struct timespec *max, struct timespec *a, struct timespec *b)
{
  if(a->tv_sec > b->tv_sec) (*max)=(*a);
  else if(a->tv_sec < b->tv_sec) (*max)=(*b);
  else{
    if(a->tv_nsec > b->tv_nsec) (*max)=(*a);
    else (*max)=(*b);
  }
}

// wait until next_activation (absolute time)
void delay_until (struct timeval* next_activation)
{
  struct timeval now, timeout;
  gettimeofday (&now, NULL);
  timeval_sub (&timeout, next_activation, &now);
  select (0, NULL, NULL, NULL, &timeout);
}



int main ()
{
  struct timeval clk_period = { 0, 250 * 1000 };
  struct timeval next_activation;
  fsm_t* cofm_fsm = fsm_new (cofm);
  fsm_t* cashm_fsm = fsm_new (cashm);

  wiringPiSetup();
  pinMode (GPIO_BUTTON, INPUT);
  pinMode (GPIO_MONEY0, INPUT);
  pinMode (GPIO_MONEY1, INPUT);
  pinMode (GPIO_MONEY2, INPUT);
  pinMode (GPIO_COIN, INPUT);
  wiringPiISR (GPIO_BUTTON, INT_EDGE_FALLING, button_isr);
  wiringPiISR (GPIO_COIN, INT_EDGE_FALLING, money_isr);
  pinMode (GPIO_CUP, OUTPUT);
  pinMode (GPIO_COFFEE, OUTPUT);
  pinMode (GPIO_MILK, OUTPUT);
  pinMode (GPIO_LED, OUTPUT);
  pinMode (GPIO_5C, OUTPUT);
  pinMode (GPIO_10C, OUTPUT);
  pinMode (GPIO_20C, OUTPUT);
  pinMode (GPIO_50C, OUTPUT);
  pinMode (GPIO_1E, OUTPUT);
  pinMode (GPIO_2E, OUTPUT);
  digitalWrite (GPIO_LED, HIGH);

	/*Variables para pruebas*/
	int mon0;
	int mon1;
	int mon2;
	int momento=1;

  /*Variables para medida de tiempos*/
  struct timespec a_spec={0, 0};
  struct timespec b_spec={0, 0};
  struct timespec cash_spec={0, 0};
  struct timespec cof_spec={0, 0};
  struct timespec cashmax_spec={0, 0};
  struct timespec cofmax_spec={0, 0};
  
  gettimeofday (&next_activation, NULL);
  while (button!=-1) {
    scanf("%d %d %d %d %d\n", &button, &mon2, &mon1, &mon0);
    actualizaMoney(mon0, mon1, mon2);
    money_isr();
    printf("%d.\n", momento);
    momento++;

    clock_gettime(CLOCK_MONOTONIC,&b_spec);
    fsm_fire (cashm_fsm);
    clock_gettime(CLOCK_MONOTONIC,&a_spec);
    timespec_sub(&cash_spec, &a_spec, &b_spec);
    timespec_max(&cashmax_spec, &cashmax_spec, &cash_spec);

    clock_gettime(CLOCK_MONOTONIC,&b_spec);
    fsm_fire (cofm_fsm);
    clock_gettime(CLOCK_MONOTONIC,&a_spec);
    timespec_sub(&cof_spec, &a_spec, &b_spec);
    timespec_max(&cofmax_spec, &cofmax_spec, &cof_spec);

    printf("Tiempo de ejecucion de cashm: %d segundos y %d nanosegundos\n",(int) cash_spec.tv_sec, (int) cash_spec.tv_nsec);
    printf("Tiempo de ejecucion de cofm: %d segundos y %d nanosegundos\n",(int) cof_spec.tv_sec, (int) cof_spec.tv_nsec);
    timeval_add (&next_activation, &next_activation, &clk_period);
    delay_until (&next_activation);
  }

  printf("Tiempo de ejecucion MAXIMO cashm: %d segundos y %d nanosegundos\n",(int) cashmax_spec.tv_sec, (int) cashmax_spec.tv_nsec);
  printf("Tiempo de ejecucion MAXIMO de cofm: %d segundos y %d nanosegundos\n",(int) cofmax_spec.tv_sec, (int) cofmax_spec.tv_nsec);

  return 0;
	
}