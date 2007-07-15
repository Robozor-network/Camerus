// **** Objeti cihly vlevo **** LLLL

#define L_TOUCH 1    // Cara vlevo
#define R_TOUCH 2    // Cata vpravo
#define B_TOUCH 3    // Both

int8 n;
int8 r1,r2,rr;
int8 touch;
enum okolo_cihly {pred_carou,na_care,po_care};
okolo_cihly ridic;       // V jakem jsme stavu objizdeni cihly
int8 vzdalenost;
int8 visualisation;

   stav=cihla; // Dalsi prekazku uz nezaznamenavej (je to s velkou pravdepodobnosti cil)
   odocounter=get_timer1();

cihla:

   rr=RR_CIHLA;   //!!! Rozumna rychlost pro objizdeni cihly (bylo by lepsi rychlost zvysovat) a pri detekci pohybu zase snizit
   disp(0x99);
   set_pwm1_duty(0);    // zabrzdi levym kolem, prave vpred
   set_pwm2_duty(255);
   output_high(MOT_L);
   output_low(MOT_R);
   while(true) // Na zacatku se vyhni cihle, zatoc co muzes
   {
      cas=CASMIN-5;       // jeste vic nez hodne do leva

      if(BUMPER) // Narazili jsme do cihly, musime couvnout!
      {
         bum();
         SaveLog();      // Zapis Black Boxu do EEPROM
         delay_ms(TUHOS); //!!! Zatuhle prevodovky
         brzda();
         goto cihla; // Znovu zacni cihlu objizdet
      };

      set_pwm1_duty(0);
      set_pwm2_duty(255);     // !!! mozna prilis maly vykon pro rozjezd pro zatuhlou prevodovku
      output_high(MOT_L);     // leve kolo reverz
      output_low(MOT_R);      // prave kolo vpred
      if(get_timer1()>(odocounter+5))  // konec zatacky?
      {
         disp(0x66);
         break;
      }
      SetServoQ(cas);
      delay_ms(18);
   };

   //------ Objeti cihly v konstantni vzdalenosti ------
   touch=0;    // Indikator detekce cary pri objizdeni
   ridic=pred_carou;
   cas=CASAVR-CASMIN;   // rovne
   output_low(MOT_L);   // vpred
   output_low(MOT_R);
   visualisation=0;
   while(true)
   {
      if(BUMPER) // Narazili jsme do cihly, musime couvnout!
      {
         bum();
         SaveLog();      // Zapis Black Boxu do EEPROM
         delay_ms(TUHOS); //!!! Zatuhle prevodovky
         set_pwm1_duty(160);  // vpred
         set_pwm2_duty(160);
         output_low(MOT_L);
         output_low(MOT_R);
         cas=CASMIN;
      };

      delta_bearing=bearing-bearing_offset;
      visualisation=(delta_bearing & 0xF0) | (visualisation & 0x0F);
      if(IRRX)  // hrozi celni srazka s cihlou v prubehu objizdeni
      {
         cas=CASMIN;
      }
      else
      {
         if((vzdalenost!=0)||!input(PROXIMITY)||((delta_bearing>60)&&(delta_bearing<128))) // Udrzovani konstantni vzdalenosti od cihly
         {
            if(cas>(CASMIN+30)) cas-=30;
         }
         else
         {
            if(cas<(CASMAX-30)) cas+=30;
         };
      };
      // Elektronicky diferencial
      if(cas<CASAVR) {r1=cas-CASMIN; r2=CASAVR-CASMIN;}; // Normovani vystupni hodnoty radkoveho snimace
      if(cas==CASAVR) {r1=cas-CASMIN; r2=cas-CASMIN;};   // pro rizeni rychlosti motoru
      if(cas>CASAVR) {r1=CASAVR-CASMIN; r2=CASMAX-cas;}; // Rozsah 1 az 92

      if (r1>(CASAVR-CASMIN-rr)) r1=(r1<<1)+rr-(CASAVR-CASMIN);     // Neco jako nasobeni
      if (r2>(CASAVR-CASMIN-rr)) r2=(r2<<1)+rr-(CASAVR-CASMIN);

//!!! pro zatuhle prevodovky
//      r1<<=1;     // Rychlost je dvojnasobna
//      r2<<=1;     // Rozsah 2 az 184 pro rr=0

      set_pwm1_duty(r1);   // Nastav rychlost motoru
      set_pwm2_duty(r2);

      SetServoQ(cas);

      i2c_start();     // Sonar Ping
      i2c_write(SONAR_ADR);
      i2c_write(0x0);
      i2c_write(0x52);  // mereni v us
      i2c_stop();

      for(n=1;n<=90;n++) // 18ms testovani cary do dalsi korekce serva
      {
         set_adc_channel(LMAX);
         delay_us(100);
         if(read_adc()<THR) touch|=L_TOUCH;
         set_adc_channel(RMAX);
         delay_us(100);
         if(read_adc()<THR) touch|=R_TOUCH;
      };

      i2c_start();     // Odraz ze sonaru
      i2c_write(SONAR_ADR);
      i2c_write(0x3);
      i2c_stop();
      i2c_start();
      i2c_write(SONAR_ADR+1);
      vzdalenost=i2c_read(0);
      i2c_stop();

      i2c_start();     // Cteni kompasu
      i2c_write(COMPAS_ADR);
      i2c_write(0x1);   // uhel 0-255
      i2c_stop();
      i2c_start();
      i2c_write(COMPAS_ADR+1);
      bearing=i2c_read(0);
      i2c_stop();

      if(touch==L_TOUCH) visualisation|=0x2;
      if(touch==R_TOUCH) visualisation|=0x1;
      if((touch==B_TOUCH)&&(ridic==pred_carou)) ridic=na_care;
      if((ridic==na_care)&&(touch==0)) break;
      if(ridic==na_care) touch=0;
      disp(visualisation);
   };
   disp(0xC3);

   set_pwm1_duty(0);    //!!! pred zatuhlejma prevodovkama tam bylo 20 a 200
   set_pwm2_duty(255);
   output_high(MOT_L);
   output_low(MOT_R);
   delay_us(40);
   odocounter=get_timer1();    // Poznamenej aktualni stav odometrie
   while (true)  // Znovu se musime dotknout cary
   {
      for(n=1;n<=90;n++) // 18ms testovani cary do dalsi korekce serva
      {
         set_adc_channel(LMAX);    // Levy UV sensor
         delay_us(100);
         if(read_adc()<THR)   // Dotkli jsme se levym senzorem
         {
            disp(0xE0);
            cas=CASAVR-CASMIN; // nastavime, ze cara je rovne
            goto cara;
         };
         set_adc_channel(RMAX);    // Pravy UV sensor
         delay_us(100);
         if((get_timer1()>=(odocounter+2)) && (read_adc()<THR)) // Pravym senzorem nesmime caru prejet!
         {
            disp(0x07);
            cas=CASMAX; // kdyz prejedem, tak nastavime, ze cara je vpravo
            goto cara;
         };
      }
      SetServoQ(CASMIN-5);   // max. max. doleva                 L
   }

cara:

   output_low(MOT_L); // oba motory vpred
   output_low(MOT_R);
