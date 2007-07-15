//********* Robot Camerus pro IstRobot 2007 ************
//"$Id: camerus.c 253 2007-04-24 09:26:46Z kakl $"
//*****************************************************

#include ".\camerus.h"

#USE FAST_IO (C)     // Brana C je ve FAST_IO modu, aby slo rychle cist z kamery

// Rychlostni konstanty
#define RR_CIHLA     50       // Rozumna rychlost pro objizdeni cihly
#define RR_PRERUSENI 50       // Rozumna rychlost pro priblizeni se k preruseni
#define BRZDNA_DRAHA 0x15     // Jak daleko pred problemem se zacne brzdit
#define TUHOS        100      // Jak dlouho se bude couvat po narazu na naraznik
#define ODODO_PROBLEM1     0xFFF
#define ODODO_PROBLEM2     0xFFF
#define ODODO_PROBLEM3     0xFFF

// Adresy IIC periferii
#define COMPAS_ADR   0xC0
#define CAMERA_ADR   0xDC
#define SONAR_ADR    0xE0

// A/D vstupy
#define  RMAX     4  // AN4/RA5 - leve cidlo na vyjeti z cary
#define  LMAX     3  // AN3/RA3 - prave cidlo na vyjeti z cary
#define  CERVENA  2  // AN2/RA2 - cervene kroutitko
#define  ZELENA   1  // AN1/RA0 - zelene kroutitko
#define  MODRA    0  // AN0/RA1 - modre kroutitko

// I/O
#define HREF      PIN_C5      // Signal HREF z kamery (v H po celou dobu radku)
#define PIX       PIN_C6      // Vstup pro body z kamery (za trivstupim hradlem OR (dig. komparator))
#define SERVO     PIN_B4      // Vystup na servo (1 az 2ms po cca 20ms (synchronizovano snimkovym kmitoctem))
#define MOT_L     PIN_B5      // Smer otaceni leveho motoru; druhy pol je RC2
#define MOT_R     PIN_B6      // Smer otaceni praveho motoru; druhy pol je RC1
#define MOT_1     PIN_C1      // PWM vystpy motoru
#define MOT_2     PIN_C2      //
#define DATA      PIN_B2      // K modulu LEDbar data
#define CP        PIN_B1      // K modulu LEDbar hodiny
//#define ODO       PIN_C0      // Ze snimace z odometrie z praveho kola na TIMER1
                                // Jeden impuls je 31,25mm
#define IRRX      !input(PIN_B0)   // Vstup INT, generuje preruseni pri prekazce
#define IRTX      PIN_B3      // Modulovani vysilaci IR LED na detekci prekazky
#define PROXIMITY PIN_C7      // Cidlo kratkeho dosahu na cihlu
#define BUMPER    !input(PIN_A4)  // Naraznik

#define CASMIN 6           // Rozsah radku snimace
#define CASMAX 192
#define CASAVR ((CASMAX+CASMIN) / 2)

#define EEMAX  255         // Konec EEPROM
#define MAXLOG 0x10        // Maximalni pocet zaznamu v logu
#if  MAXLOG>(EEMAX/3)
   #error  Prekrocena velikost EEPROM
#endif

#define OFFSETO   0x9F //0x9F         // Vystredeni serva pro objeti prekazky

#define THR 90    // Threshold pro UV cidla na caru

#byte INTCON      = 0x0B         // Interrupt configuration register
   #bit GIE       = INTCON.7
   #bit PEIE      = INTCON.6
   #bit TMR0IE    = INTCON.5
   #bit INT0IE    = INTCON.4
   #bit RBIE      = INTCON.3
   #bit TMR0IF    = INTCON.2
   #bit INT0IF    = INTCON.1
   #bit RBIF      = INTCON.0

enum stavy {start,rozjezd,jizda,cihla,pocihle,cil};
stavy stav;       // Kde jsme na trati
int8 cas;         // Cas hrany bila/cerna v radce
int8 stred;       // Vystredeni kolecka
int16 odocounter; // Zaznamenani aktualniho stavu pocitadla odometrie
int16 last_log_odo; // Posledni stav odometrie poznamenany do logu
int16 last_log;   // Cislo posledniho zaznamu v logu v EEPROM
int8 bb_h[MAXLOG];  // Cerna skrinka MSB
int8 bb_l[MAXLOG];  // Cerna skrinka LSB
int8 bb_f[MAXLOG];  // Cerna skrinka priznak (typ zaznamu)
int8 log;         // Pocitadlo pro cernou skrinku
int8 rr;          // Promenna na ulozeni Rozumne rychlost
int8 rrold;
int16 odo_preruseni, odo_cihla, odo_tunel; // Problemy na trati
int16 odo_problem1, odo_problem2, odo_problem3; // Problemy na trati

// Zobrazeni jednoho byte na modulu LEDbar
inline void disp(int8 x)
{
   int n;

   for(n=0;n<=7;n++)
   {
      if (bit_test(x,0)) output_low(DATA); else output_high(DATA);
      output_high(CP);
      x>>=1;
      output_low(CP);
   }
}

// Blikani LEDbarem ve stilu Night Rider
void NightRider(int8 x)
{
   int n,i,j;

   for(j=0;j<x;j++)
   {
      i=0x01;
      for(n=0;n<7;n++)
      {
         disp(i);
         rotate_left(&i, 1);
         delay_ms(40);
      }
      for(n=0;n<7;n++)
      {
         disp(i);
         rotate_right(&i, 1);
         delay_ms(40);
      }
   }
   disp(i);
   delay_ms(40);
   i=0;
   disp(i);
}

// Zaznam LOGu do EEPROM
void SaveLog()
{
   int8 n,i,xlog;

   i=0;
   for(n=0;n<=(log*3);n+=3)   // Ulozeni Black Boxu do EEPROM
   {
      write_eeprom(n,bb_f[i]);
      write_eeprom(n+1,bb_h[i]);
      write_eeprom(n+2,bb_l[i]);
      i++;
   };
   if(log>0) {xlog=log-1;} else {xlog=0;};
   write_eeprom(EEMAX,xlog);  // Zapis poctu zaznamu na konec EEPROM
}

// Zaznam do Logu do RAM
void LogLog(int8 reason, int16 log_delay)
{
   int16 timer_pom;

   timer_pom=get_timer1();          // Timer se musi vycist atomicky
   bb_l[log]=make8(timer_pom,0); // Zaznam
   bb_h[log]=make8(timer_pom,1);
   bb_f[log]=reason;   // Typ zaznamu
   if(log<(MAXLOG-1)) log++;         // Ukazatel na dalsi zaznam
   last_log_odo=timer_pom+log_delay;     // Dalsi mereni nejdrive po ujeti def. vzdalenosti
   rr=rrold;      // Problem skoncil, znovu jed Rozumnou Rychlosti
}

void ReadBlackBox()
{
   last_log=read_eeprom(EEMAX); // Kolik zaznamu mame od minule poznamenano?
   {
      int8 n,i;

      i=0;
      for(n=0;n<=last_log;n++)
      {
         if(read_eeprom(i)==0) odo_tunel=MAKE16(read_eeprom(i+2),read_eeprom(i+1));
         if(read_eeprom(i)==0xFF) odo_cihla=MAKE16(read_eeprom(i+2),read_eeprom(i+1));
         if((read_eeprom(i)>0) && (read_eeprom(i)<0xFF)) odo_preruseni=MAKE16(read_eeprom(i+2),read_eeprom(i+1));
      }
   }
}


// Brzdeni motorama stridou 1:1
void brzda()
{
   int8 n,i;

   set_pwm1_duty(0);    // vypni PWM
   set_pwm2_duty(0);
   setup_ccp1(CCP_OFF);
   setup_ccp2(CCP_OFF);
   for (n=0;n<200;n++)
   {
      output_low(MOT_L);
      output_low(MOT_R);
      output_high(MOT_1);
      output_high(MOT_2);
      delay_us(200);
      output_high(MOT_L);
      output_high(MOT_R);
      output_low(MOT_1);
      output_low(MOT_2);
      delay_us(200);
   }
   output_low(MOT_L); // smer vpred
   output_low(MOT_R);
   setup_ccp1(CCP_PWM); // RC1               // Zapni PWM pro motory
   setup_ccp2(CCP_PWM); // RC2
}

void SetServo(int8 angle)
{
   int8 n;

   for(n=0; n<10; n++)
   {
      output_high(SERVO);        // Odvysilani impuzu 1 az 2ms pro servo
      delay_us(1000);
      delay_us(stred);
      delay_us(stred);
      delay_us(stred);
      delay_us(angle);
      delay_us(angle);
      output_low(SERVO);
      delay_ms(18);
   }
}

inline void SetServoQ(int8 angle)
{
   output_high(SERVO);        // Odvysilani impuzu 1 az 2ms pro servo
   delay_us(1000);
   delay_us(stred);
   delay_us(stred);
   delay_us(stred);
   delay_us(angle);
   delay_us(angle);
   output_low(SERVO);
}

// Couvni po narazu na naraznik
inline void bum()
{
   set_pwm1_duty(0);    // couvni, rovne dozadu
   set_pwm2_duty(0);
   output_high(MOT_L);
   output_high(MOT_R);
   disp(0xA5);
   SetServo(CASAVR-CASMIN);
}

#include ".\diag.c"

//---------------------------- INT --------------------------------
#int_EXT
EXT_isr()   // Preruseni od prekazky
{
   unsigned int8 bearing, bearing_offset, delta_bearing;

   set_pwm1_duty(0);    // zabrzdi levym kolem, prave vypni
   set_pwm2_duty(0);
   output_high(MOT_L);
   output_low(MOT_R);
   // Ujistime se, ze prijaty signal je z naseho IR vysilace
   output_high(IRTX);   // Vypni LED na detekci prekazky
   delay_ms(2);
   if (IRRX)    // stale nas signal?
   {
      output_low(MOT_L); // je odraz -> vpred
      output_low(MOT_R);
      return;
   };
   output_low(IRTX);    // Zapni LED na detekci prekazky

   i2c_start();     // Cteni kompasu
   i2c_write(COMPAS_ADR);
   i2c_write(0x1);   // 0-255 (odpovida 0-359)
   i2c_stop();
   i2c_start();
   i2c_write(COMPAS_ADR+1);
   bearing_offset=i2c_read(0); // Poznamenej hodnotu pred cihlou
   i2c_stop();

   delay_ms(9);
   if (!IRRX)     // stale nas signal?
   {
      output_low(MOT_L); // neni odraz -> vpred
      output_low(MOT_R);
      return;
   };

   rr=rrold;      // Po cihle se pojede opet Rozumnou Rychlosti
   if(stav!=cihla)
   {
      LogLog(0xFF,3);   // Cihla
   };

//!!!   if(stav==cihla) while(true); // Zastav na furt, konec drahy
//   if(stav==cihla) return; // Po druhe nic neobjizdej
// Pozor na rozjezd

   if((stav==jizda)||(stav==cihla))   // Objed cihlu
   {
      #include ".\objizdka_L.c"
   };
   last_log_odo=get_timer1()+16;  // Pul metru po cihle nezaznamenavej do LOGu
}


//---------------------------------- MAIN --------------------------------------
void main()
{
   int8 offset;   // Promena pro ulozeni offsetu
   int8 r1;       // Rychlost motoru 1
   int8 r2;       // Rychlost motoru 2
   int16 ble;

   setup_adc_ports(ALL_ANALOG);              // Zapnuti A/D prevodniku pro cteni kroutitek
   setup_adc(ADC_CLOCK_INTERNAL);
   setup_timer_0(RTCC_INTERNAL|RTCC_DIV_1);  // Casovac pro mereni casu hrany W/B v radce
   setup_timer_1(T1_EXTERNAL);               // Cita pulzy z odometrie z praveho kola
   setup_timer_2(T2_DIV_BY_16,255,1);        // Casovac PWM motoru
//!!!   setup_timer_2(T2_DIV_BY_4,255,1);        // Casovac PWM motoru
   setup_ccp1(CCP_PWM); // RC1               // PWM pro motory
   setup_ccp2(CCP_PWM); // RC2
   setup_comparator(NC_NC_NC_NC);
   setup_vref(FALSE);

   set_tris_c(0b11111001);     // Nastaveni vstup/vystup pro branu C, protoze se to nedela automaticky

   set_pwm1_duty(0);    // Zastav motory
   set_pwm2_duty(0);
   output_low(MOT_L);   // Nastav smer vpred
   output_low(MOT_R);

   disp(0);    // Zhasni LEDbar

   if(BUMPER)  // Kdyz nekdo na zacatku drzi naraznik, vymaz log a spust diagnostiku
   {
      diag();
   }

   output_low(IRTX);   // Zapni LED na detekci prekazky

   NightRider(1);    // Zablikej, aby se poznalo, ze byl RESET
                     // Zaroven se musi pockat, nez se rozjede kamera, nez se do ni zacnou posilat prikazy

   //... Nastaveni sonaru ...
   i2c_start();
   i2c_write(SONAR_ADR);
   i2c_write(0x02);  // dosah
   i2c_write(0x03);  // n*43mm
   i2c_stop();
   i2c_start();
   i2c_write(SONAR_ADR);
   i2c_write(0x01);  // zesileni
   i2c_write(0x01);  // male, pro eliminaci echa z minuleho mereni
   i2c_stop();

   //... Nastaveni kamery ...
   i2c_start();      // Soft RESET kamery
   i2c_write(CAMERA_ADR);        // Adresa kamery
   i2c_write(0x12);        // Adresa registru COMH
   i2c_write(0x80 | 0x24); // Zapis ridiciho slova
   i2c_stop();

   i2c_start();      // BW
   i2c_write(CAMERA_ADR);
   i2c_write(0x28);
   i2c_write(0b01000001);
   i2c_stop();

/*
   i2c_start();      // Contrast (nema podstatny vliv na obraz)
   i2c_write(CAMERA_ADR);
   i2c_write(0x05);
   i2c_write(0xA0);  // 48h
   i2c_stop();

   i2c_start();      // Band Filter (pokud by byl problem se zarivkama 50Hz)
   i2c_write(CAMERA_ADR);
   i2c_write(0x2D);
   i2c_write(0x04 | 0x03);
   i2c_stop();
*/

   i2c_start();      // Fame Rate
   i2c_write(CAMERA_ADR);
   i2c_write(0x2B);
   i2c_write(0x00);  // cca 17ms (puvodni hodnota 5Eh = 20ms)
   i2c_stop();

   i2c_start();      // VSTRT
   i2c_write(CAMERA_ADR);
   i2c_write(0x19);
   i2c_write(118);   // prostredni radka
   i2c_stop();

   i2c_start();      // VEND
   i2c_write(CAMERA_ADR);
   i2c_write(0x1A);
   i2c_write(118);
   i2c_stop();

   NightRider(1);    // Musi se dat cas kamere na AGC a AEC

   { // Mereni expozice
      int8 t1,t2;

      i2c_start();      // Brightness, zacni od uplne tmy
      i2c_write(CAMERA_ADR);
      i2c_write(0x06);
      i2c_write(0);  // 80h default
      i2c_stop();
      delay_ms(50);

      for(offset=0x04;offset<(255-0x04);offset+=0x04)   // Zacni od jasu 10h
      {
         i2c_start();      // Brightness
         i2c_write(CAMERA_ADR);
         i2c_write(0x06);
         i2c_write(offset);  // 80h default
         i2c_stop();
         disp(offset);
         delay_ms(50);

         t1=0;
         t2=0;
         while(!input(HREF));    // Cekej nez se zacnou posilat pixely z radky
         delay_ms(5);
         while(!input(HREF));    // Cekej nez se zacnou posilat pixely z radky
         set_timer0(0);          // Vynuluj pocitadlo casu
         if(!input(PIX)) continue;
         while(input(PIX));
         t1=get_timer0();    // Precti cas z citace casu hrany
         set_timer0(0);          // Vynuluj pocitadlo casu
         while(!input(PIX));
         t2=get_timer0();

         if((t1>60) && (t1<140) && (t2>5) && (t2<=10)) break; // Vidis, co mas?

         delay_ms(2);   // Preskoc druhou radku z kamery
      };
      delay_ms(1000);   // Nech chvili na displayi zmerenou hodnotu
   }

   set_adc_channel(CERVENA);   // --- Kroutitko pro jas ---
   delay_ms(1);
   offset=read_adc();
   offset &= 0b11111100;   // Dva nejnizsi bity ignoruj
//   offset += 0x70;         // Jas nebude nikdy nizsi
   disp(offset);
   i2c_start();            // Brightness
   i2c_write(CAMERA_adr);
   i2c_write(0x06);
   i2c_write(offset);  // 80h default
   i2c_stop();
   delay_ms(1000);   // Nech hodnotu chvili na displayi

   set_adc_channel(ZELENA);   // --- Kroutitko pro vykon motoru ---
   delay_ms(1);
   rr=read_adc()>>2; // 0-63  // Pokud by se zvetsil rozsah, tak zkontrolovat jakonasobeni!
   rr+=27;  // 27-90
//!!!   rr=read_adc()>>1; // 0-128 // Pokud by se zvetsil rozsah, tak zkontrolovat jakonasobeni!
   rrold=rr;

   cas=CASAVR-CASMIN;   // Inicializace promenych, aby neslo servo za roh
                        // a aby se to rozjelo jeste dneska
   stav=start;          // Jsme na startu
   set_timer1(0);       // Vynuluj citac odometrie
   log=0;               // Zacatek logu v cerne skrince
   last_log_odo=0;      // Posledni zaznam odometrie do logu

   ReadBlackBox();    // Vycteni zaznamu z Black Boxu

   odo_problem1=ODODO_PROBLEM1-BRZDNA_DRAHA;
   odo_problem2=ODODO_PROBLEM2-BRZDNA_DRAHA;
   odo_problem3=ODODO_PROBLEM3-BRZDNA_DRAHA;

   // ........................... Hlavni smycka ................................
   while(true)
   {
      int8 pom;
      int8 n;
      int8 gap;
      int16 ododo;

      gap=0;      // Vynuluj pocitadlo preruseni

next_snap:

      pom=0;
      disable_interrupts(GLOBAL);  //----------------------- Critical Section
      while(input(HREF));     // Preskoc 1. radku
      while(!input(HREF));    // Cekej nez se zacnou posilat pixely z 2. radky
      set_timer0(0);          // Vynuluj pocitadlo casu
      while(input(HREF))      // Po dobu vysilani radky cekej na hranu W/B
      {
// !!!!Dodelat rozpoznani cerne cary napric pro zastaveni ?
         if(!input(PIX))   // Pokud se X-krat za sebou precetla CERNA
         if(!input(PIX))
//         if(!input(PIX))
         {
            pom=get_timer0();    // Precti cas z citace casu hrany
            break;
         };
      };
      while(input(HREF));      // Pockej na shozeni signalu HREF

      if((pom<CASMAX) && (pom>CASMIN)) cas=pom;  // Orizni konce radku
      // Na konci obrazovaho radku to blbne. Jednak chyba od apertury
      // a vubec to nejak na kraji nefunguje.

      output_high(SERVO);        // Odvysilani impuzu 1 az 2ms pro servo
      delay_us(1000);
      delay_us(stred);
      delay_us(stred);
      delay_us(stred);
      delay_us(cas);
      delay_us(cas);
      output_low(SERVO);

      // Elektronicky diferencial 1. cast
      if(cas<CASAVR) {r1=cas-CASMIN; r2=CASAVR-CASMIN;}; // Normovani vystupni hodnoty radkoveho snimace
      if(cas==CASAVR) {r1=cas-CASMIN; r2=cas-CASMIN;};   // pro rizeni rychlosti motoru
      if(cas>CASAVR) {r1=CASAVR-CASMIN; r2=CASMAX-cas;}; // Rozsah 1 az 92

      enable_interrupts(GLOBAL);    //----------------------- End Critical Section

      if(pom==0) // Kamera nevidi caru
      {
         if((cas>(CASMIN+15))&&(cas<(CASMAX-15))) // Nebyla minule cara moc u kraje?
         {
            gap++;
            if(gap>=3) // Trva preruseni cary alespon 2 snimky?
            {
               cas=CASAVR-CASMIN;
//               disp(0xAA);
            }
         }
      }
      else
      {
         gap=0;
      };


/*
      if(pom==0) // Kamera nevidi caru, poznamenej to do logu
      {
         if((cas>(CASMIN+30))&&(cas<(CASMAX-30))) // Nebyla minule cara moc u kraje?
         if(last_log_odo<get_timer1()) // Aby nebyly zaznamy v logu prilis huste, musi se napred neco ujet od minuleho zaznamu
         {
            gap++;
         }
      }
      else
      {
         if(gap>=4) // Trva preruseni cary alespon 2 snimky?
         {
            LogLog(gap,8); // Dalsi mereni nejdrive po ujeti 24 cm
            rr=rrold;      // Preruseni cary skoncilo, znovu jed Rozumnou Rychlosti
            cas=CASAVR-CASMIN;
            disp(0xAA);
         }
         gap=0;
      };

      if(!input(PROXIMITY) && ((stav==jizda)||(stav==cihla))) // Tunel
      {
         if(last_log_odo<get_timer1()) // Aby nebyly zaznamy v logu prilis huste, musi se napred neco ujet od minuleho zaznamu
         {
            LogLog(0xDD,16);   // Priznak tunelu; dalsi mereni nejdrive po ujeti 48 cm
            rr=rrold;      // Vjeli jsme do tunelu, znovu jed rychle
         }
      };
*/

//ODODO
      ododo=get_timer1();
      if((ododo>odo_preruseni)&&(ododo<(odo_preruseni+8))) rr=RR_PRERUSENI;
      if((ododo>odo_cihla)&&(ododo<(odo_cihla+8))) rr=RR_PRERUSENI;
      if((ododo>odo_tunel)&&(ododo<(odo_tunel+8))) rr=RR_PRERUSENI;
      if((ododo>odo_problem1)&&(ododo<(odo_problem1+8))) rr=RR_PRERUSENI;
      if((ododo>odo_problem2)&&(ododo<(odo_problem2+8))) rr=RR_PRERUSENI;
      if((ododo>odo_problem3)&&(ododo<(odo_problem3+8))) rr=RR_PRERUSENI;

      // Elektronicky diferencial 2. cast
      if (r1>(CASAVR-CASMIN-rr)) r1=(r1<<1)+rr-(CASAVR-CASMIN);     // Neco jako nasobeni
      if (r2>(CASAVR-CASMIN-rr)) r2=(r2<<1)+rr-(CASAVR-CASMIN);     // rozsah 1 az 92 pro rr=0                                                                   // rozsah 1 az 154 pro rr=63

//!!! pro zatuhle prevodovky
//      r1<<=1;     // Rychlost je dvojnasobna
//      r2<<=1;     // Rozsah 2 az 184 pro rr=0

      if ((stav==jizda)||(stav==cihla)||(stav==rozjezd)) //||(stav==pocihle))      // Jizda
      {
         set_pwm1_duty(r1);
         set_pwm2_duty(r2);
      }
      else
      {
         set_pwm1_duty(0);       // Zastaveni
         set_pwm2_duty(0);
      };

      if((stav==rozjezd)&&(get_timer1()>10)) // musi ujet alespon 31cm
      {
         ext_int_edge(H_TO_L);         // Nastav podminky preruseni od cihly
         INT0IF=0;                     // Zruseni predesle udalosti od startera
         enable_interrupts(INT_EXT);
         enable_interrupts(GLOBAL);
         stav=jizda;
      };

      if(stav==start)         // Snimkuje, toci servem a ceka na start
      {
         set_adc_channel(MODRA);    // Kroutitko na vystredeni predniho kolecka
         Delay_ms(1);
         stred=read_adc();
         if(!input(PROXIMITY))
         {
            disp(0x80);
            while(input(PROXIMITY)); // Cekej, dokud starter neda ruku pryc
            set_timer1(0);         // Vynuluj citac odometrie
            set_pwm1_duty(255);    // Rychly rozjezd  !!! Zkontrolovat na oscyloskopu
            set_pwm2_duty(255);
            disp(0x01);
            while(get_timer1()<=4) // Ujed alespon 12cm
            {
               set_adc_channel(LMAX);    // Levy UV sensor
               delay_us(40);
               if(read_adc()<THR) {cas=CASMIN; break;};  // Prejeli jsme caru vlevo
               set_adc_channel(RMAX);    // Pravy UV sensor
               delay_us(40);
               if(read_adc()<THR) {cas=CASMAX; break;};  // Prejeli jsme caru vpravo
               cas=CASAVR-CASMIN;    // Cara je rovne
            };
            stav=rozjezd;
         };
      }

      pom=0x80;    // Zobrazeni pozice cary na displayi
      for(n=CASMAX/8; n<cas; n+=CASMAX/8) pom>>=1;
      disp(pom);

      while(true) // Ve zbytku casu snimku cti krajni UV senzory a naraznik
      {
         if(BUMPER) // Sakra, do neceho jsme narazili a nevideli jsme to!
         {
            bum();
            SaveLog();      // Zapis Black Boxu do EEPROM
            delay_ms(TUHOS); //!!! Zatuhle prevodovky
            set_pwm1_duty(200);   // pomalu vpred
            set_pwm2_duty(200);
            output_low(MOT_L);
            output_low(MOT_R);
            cas=CASAVR-CASMIN;
         };
         set_adc_channel(LMAX);    // Levy UV sensor
         for(n=0;n<20;n++) if(input(HREF)) goto next_snap;
         if(read_adc()<THR) cas=CASMIN;
         set_adc_channel(RMAX);    // Pravy UV sensor
         for(n=0;n<20;n++) if(input(HREF)) goto next_snap;
         if(read_adc()<THR) cas=CASMAX;
      };
   }
}
