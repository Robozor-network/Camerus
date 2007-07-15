//--- Diagnostika cidel a vymazani EEPROM ---
void diag()
{
   int8 n;

   // Vymaz Black Box v EEPROM
   for(n=0;n<255;n++) write_eeprom(n,0);
   bb_l[0]=0;        // Zapis na pozici 0 vzdalenost 0
   bb_h[0]=0;
   bb_f[0]=0;
   write_eeprom(EEMAX,0); // Zapis do EEPROM pocet zaznamu 0, tedy jeden zaznam
   for(n=0;n<=4;n++)
   {
      disp(0x55);       // Blikni pro potvrzeni
      delay_ms(200);
      disp(0xAA);
      delay_ms(200);
   };

   while(true)
   {
      if(!IRRX)
      {
         int8 ble;

         i2c_start();     // Cteni kompasu
         i2c_write(COMPAS_ADR);
         i2c_write(0x1);
         i2c_stop();
         i2c_start();
         i2c_write(COMPAS_ADR+1);
         ble=i2c_read(0);
         i2c_stop();
         disp(ble);
         delay_ms(200);
      }
      else
      {
         i2c_start();      // Diagnostika sonaru
         i2c_write(SONAR_ADR);
         i2c_write(0x02);  // dosah
         i2c_write(0x03);  // n*43mm
         i2c_stop();
         i2c_start();
         i2c_write(SONAR_ADR);
         i2c_write(0x01);  // zesileni
         i2c_write(0x01);  // male, pro eliminaci echa z minuleho mereni
         i2c_stop();

         i2c_start();     // Sonar Ping
         i2c_write(0xE0);
         i2c_write(0x0);
         i2c_write(0x51);  // 50 mereni v palcich, 51 mereni v cm, 52 v us
         i2c_stop();
         delay_ms(100);
         i2c_start();     // Odraz ze sonaru
         i2c_write(0xE0);
         i2c_write(0x3);
         i2c_stop();
         i2c_start();
         i2c_write(0xE1);
         n=i2c_read(0);
         i2c_stop();
         disp(n);  // Zobrazeni hodnoty ze sonaru a zaroven diagnostika predniho IR cidla
         delay_ms(200);
      }
   }
}
