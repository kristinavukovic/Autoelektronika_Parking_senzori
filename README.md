PARKING SENZOR

Zadatak projekta: Simulacija sistema za parking senzore u automobilima. Projekat je realizovan kodom koji je napisan u 
Visual Studio 2019 programu uz pomoć FREE RTOS biblioteka.

IDEJA I ZADACI:
1. Potrebno je mjeriti udaljenost na 2 senzora-LIJEVI i DESNI koji se dobijaju preko kanala 0 i 1.
2. Potrebno je izvršiti kalibraciju oba senzora kroz jednačinu prave kroz dvije tačke na sljedeći način:
   potrebno je postaviti objekat na određenu udaljenost ispred senzora i preko PC-ja poslati očekivani nivo u procentima.
   Postupak je potrebno ponoviti za oba senzora odvojeno.
3. Uključivanje i isključivanje sistema se vrši preko kanala 2
4. Koristi se automatski mjenjač i sistem treba da bude uključen samo kada stigne poruka "REVERSE", a u ostalim slučajevima isključen
    ("PARK", "DRIVE" i "NEUTRAL").
5. Uključivanje sistema ispratiti preko LED bara, koji simulira prekidač
   (iskoristiti jedan stubac u LED baru).
6. Jedna LED dioda treba da simulira uključenost sistema (iskoristiti drugi stubac u LED baru).
   Nazvana je LED_sistem_aktivan.
7. Treći stubac LED bara treba da simulira relevantan podatak, tj. da prikazije vrijednost sa senzora koja je manja u odnosu na
   vrijednost dobijenu sa drugog senzora.
8. Ako je udaljenost veća od 100% ispisuje se zona NEMA_DETEKCIJE i ne generiše se vizuelni signal; ako je vrijednost između 20% i 100%
   potrebno je uključiti određeni broj LED dioda; ako je udaljenost manja od 20% ispisuje se zona KONTAKT_DETEKCIJA i generisu se sve diode.
9. Slanje izmjena udaljenosti se vrši na svakih 5s u obliku prethodno navedenih zona detekcije.

NAČIN POKRETANJA:
Najprije je potrebno pokrenuti periferije na sljedeći način: u Command Prompt-u otvoriti najprije potrebne kanale (0, 1 i 2), zatim LED bar
i 7 segmentni displej.
Zatim pokrenuti program. 
U prozoru LED bara plave LED diode simuliraju udaljenost sa senzora, zelena LED dioda (0x02) simulira stanje uključenost/isključenost sistema, a
crvena LED dioda (0x01) simulira prekidač preko koga se vrši aktivacija sistema.
Na terminalu se ispisuju vrijednosti sa oba senzora, kao i odgovarajuće zone. 
Potrebno je najprije na kanalu 2 u dijelu "Transmitter" poslati minimalne i maksimalne vrijednosti za senzore. Poruke su formata:
"KALIBRACIJA_LIJEVI_200mm_0%", "KALIBRACIJA_LIJEVI_1000mm_100%", "KALIBRACIJA_DESNI_200mm_0%" i "KALIBRACIJA_DESNI_1000mm_100%". 
Kad je to urađeno pokreće se sistem na jedan od 2 moguća načina:
  1. Na kanalu 2 poslati kod: "REVERSE\0d" ili
  2. Klikom na crvenu diodu u LED baru
Zatim je potrebno poslati proizvoljne vrijednosti za simulaciju udaljenosti senzora sa kanala 0 (koji simulira lijevi senzor) i sa kanala 1
(koji dimulira desni senzor). Primjer: na kanalu 0 (500\0d) i na kanalu jedan (600\0d). To simulira da je udaljenost lijevog senzora od
prepreke 500mm, a desnog 600mm.
Kad se to pošalje na terminalu se ispise udaljenost u procentima, sto se takođe ispisuje i na 7 segmentnom displeju (displej je podijeljen
na taj način da je na sredini crtica, lijevi dio displeja simulira lijevi senzor, a desni-desni). Kad se izvrši kalibracija i potrebno računjanje
na LED baru (plave LED diode) vizuelno simuliraju udaljenost "kritičnijeg" senzora.
Na kanalu 2 u dijelu "Receiver" se na svakih 5s ispisuje stanje na senzorima, kao i zone.
Ako je zona KONTAKT_DETEKCIJA na 7 segmentnom displeju će se za taj senzor ispisivati crtice. U ostalim slučajevima će to biti procenti.
Sistem isključujemo tako sto na kanalu 2 umjesto "REVERSE\0d" upisujemo "PARK\0d" (ili DRIVE ili NEUTRAL).

KALIBRACIJA se vrši na sljedeći način:
    KALIBRACIJA_SENZOR_brojmm_100%\0d - vrijednost dobijena sa senzora postaje maksimum preko koga se racuna kalibracija 
    tj. ta vrednostpostaje 100%
    KALIBRACIJA_SENZOR_brojmm_0%\0d - vrijednost dobijena sa senzora postaje MINIMUM preko koga se racuna kalibracija 
    tj. ta vrednostpostaje 0%

TASKOVI:
Za potrebe realizacije koda korišćeni su sljedeći taskovi:

void SerialReceive_Task(void* pvParameters) – Prijem podataka sa kanala 0 (lijevi senzor).
void SerialReceive_Task1(void* pvParameters) – Prijem podataka sa kanala 1 (desni senzor).
void SerialReceive_Task2(void* pvParameters) – Prijem komandi sa kanala 2 (PC terminal).
void Kalibracija_kanal(void* pvParameters) – Obrada primljenih komandi i upravljanje stanjima sistema.
void racunanje_task(void* pvParameters) – Preračunavanje milimetara u procente i određivanje zona detekcije.
void LED_bar(void* pvParameters) – Kontrola LED dioda na osnovu izračunate udaljenosti.
void LCD_Displej(void* pvParams) – Ispisivanje procenata na 7-segmentnim displejima.
void LED_sistem_aktivan(void* pvParameters) – Aktivacija sistema putem hardverskog prekida (dioda/taster).
void PC_Reporting_Task(void* pvParameters) – Periodično slanje izvještaja o stanju senzora na terminal.


    
