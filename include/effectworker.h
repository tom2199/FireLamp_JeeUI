/*
Copyright © 2020 Dmytro Korniienko (kDn)
JeeUI2 lib used under MIT License Copyright (c) 2019 Marsel Akhkamov

    This file is part of FireLamp_JeeUI.

    FireLamp_JeeUI is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FireLamp_JeeUI is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FireLamp_JeeUI.  If not, see <https://www.gnu.org/licenses/>.

  (Этот файл — часть FireLamp_JeeUI.

   FireLamp_JeeUI - свободная программа: вы можете перераспространять ее и/или
   изменять ее на условиях Стандартной общественной лицензии GNU в том виде,
   в каком она была опубликована Фондом свободного программного обеспечения;
   либо версии 3 лицензии, либо (по вашему выбору) любой более поздней
   версии.

   FireLamp_JeeUI распространяется в надежде, что она будет полезной,
   но БЕЗО ВСЯКИХ ГАРАНТИЙ; даже без неявной гарантии ТОВАРНОГО ВИДА
   или ПРИГОДНОСТИ ДЛЯ ОПРЕДЕЛЕННЫХ ЦЕЛЕЙ. Подробнее см. в Стандартной
   общественной лицензии GNU.

   Вы должны были получить копию Стандартной общественной лицензии GNU
   вместе с этой программой. Если это не так, см.
   <https://www.gnu.org/licenses/>.)
*/

#pragma once

#include <Arduino.h>
#include "LList.h"
#include <ArduinoJson.h>

#ifdef ESP8266
 #include <LittleFS.h>
#endif

#ifdef ESP32
 #include <LITTLEFS.h>
 #define FORMAT_LITTLEFS_IF_FAILED true
 #define LittleFS LITTLEFS
#endif

#include "effects_types.h"

typedef struct {
    union {
        uint16_t flags;
        struct {
            bool isMicOn:1;
            bool isDebug:1;
            bool isRandDemo:1;
        };
    };
} LAMPSTATE;

typedef union {
    uint8_t mask;
    struct {
        bool canBeSelected:1;
        bool isFavorite:1;
    };
} EFFFLAGS;

typedef enum : uint8_t {ST_BASE=0,ST_END, ST_IDX, ST_AB, ST_AB2, ST_MIC} SORT_TYPE; // виды сортировки

class UIControl{
private:
    uint8_t id;
    CONTROL_TYPE ctype;
    String control_name;
    String val;
    String min;
    String max;
    String step;
public:
    UIControl(
        uint8_t _id=0,
        CONTROL_TYPE _ctype=CONTROL_TYPE::RANGE,
        const String _name="ctrl",
        const String _val="128",
        const String _min="1",
        const String _max="255",
        const String _step="1"
        ) : 
        id(_id), ctype(_ctype),
        control_name(_name),
        val(_val), min(_min), max(_max), step(_step) {}

    const uint8_t getId() {return id;}
    const CONTROL_TYPE getType() {return ctype;}
    const String &getName() {return control_name;}
    const String &getVal() {return val;}
    const String &getMin() {return min;}
    const String &getMax() {return max;}
    const String &getStep() {return step;}

    void setVal(const String &_val) {val=_val;}
};



class EffectListElem{
private:
    uint8_t ms = micros()|0xFF; // момент создания элемента, для сортировки в порядке следования (естественно, что байта мало, но экономим память)
#ifdef CASHED_EFFECTS_NAMES
    String name;
    void initName(uint16_t nb) {
        uint16_t swapnb = nb>>8|nb<<8; // меняю местами 2 байта, так чтобы копии/верисии эффекта оказалась в имени файла позади
        String filename;
        char buffer[5];
        filename.concat(F("/eff/"));
        sprintf_P(buffer,PSTR("%04x"), swapnb);
        filename.concat(buffer);
        filename.concat(F(".json"));

        DynamicJsonDocument doc(2048);
        bool ok = false;

        File jfile = LittleFS.open(filename.c_str(), "r");
        DeserializationError error;
        if (jfile){
            error = deserializeJson(doc, jfile);
            jfile.close();
        } else {
            ok = false;
        }

        if (error) {
            LOG(printf_P, PSTR("File: failed to load json file: %s, deserializeJson error: "), filename.c_str());
            LOG(println, error.code());
            ok = false;
        }
        ok = true;

        if (ok && doc[F("name")]){
            name = doc[F("name")].as<String>(); // перенакрываем именем из конфига, если есть
        } else if(!ok) {
            // LittleFS.remove(filename);
            // savedefaulteffconfig(nb, filename);   // пробуем перегенерировать поврежденный конфиг
            name = FPSTR(T_EFFNAMEID[(uint8_t)nb]);   // выбираем имя по-умолчанию из флеша если конфиг поврежден
        }
    }
#endif
public:
    uint16_t eff_nb; // номер эффекта, для копий наращиваем старший байт
    EFFFLAGS flags; // флаги эффекта

    EffectListElem(uint16_t nb, uint8_t mask){
        eff_nb = nb;
        flags.mask = mask;
#ifdef CASHED_EFFECTS_NAMES
        initName(nb);
#endif
    }

    EffectListElem(const EffectListElem *base){
        eff_nb = ((((base->eff_nb >> 8) + 1 ) << 8 ) | (base->eff_nb&0xFF)); // в старшем байте увеличиваем значение на 1
        flags = base->flags;
#ifdef CASHED_EFFECTS_NAMES
        initName(base->eff_nb);
#endif
    }

    bool canBeSelected(){ return flags.canBeSelected; }
    void canBeSelected(bool val){ flags.canBeSelected = val; }
    bool isFavorite(){ return flags.isFavorite; }
    void isFavorite(bool val){ flags.isFavorite = val; }
    uint8_t getMS(){ return ms; }
#ifdef CASHED_EFFECTS_NAMES
    String& getName() {return name;}
    void setName(const String& _name) {name = _name;}
#endif
};


// forward declaration
class EffectWorker;

//! Basic Effect Calc class
/**
 * Базовый класс эффекта с основными переменными и методами общими для всех эффектов
 * методы переопределяются каждым эффектом по необходимости
*/
class EffectCalc {
private:
    EffectWorker *pworker = nullptr; // указатель на воркер
    LAMPSTATE *lampstate = nullptr;
    LList<UIControl *> *ctrls;
    String dummy; // дефолтная затычка для отсутствующего контролла, в случае приведения к целому получится "0"
    bool active = false;          /**< работает ли воркер и был ли обсчет кадров с момента последнего вызова, пока нужно чтобы пропускать холостые кадры */
    bool isCtrlPallete = false; // признак наличия контрола палитры
    bool isMicActive = false; // признак включенного микрофона
protected:
    EFF_ENUM effect;        /**< энумератор эффекта */
    bool isDebug() {return lampstate!=nullptr ? lampstate->isDebug : false;}
    bool isRandDemo() {return lampstate!=nullptr ? lampstate->isRandDemo : false;}
    bool isActive() {return active;}
    void setActive(bool flag) {active=flag;}
    uint32_t lastrun=0;     /**< счетчик времени для эффектов с "задержкой" */
    byte brightness;
    byte speed;
    byte scale;

    uint8_t palettescale;       // внутренний масштаб для палитр, т.е. при 22 палитрах на нее будет приходится около 11 пунктов, при 8 палитрах - около 31 пункта
    float ptPallete;            // сколько пунктов приходится на одну палитру; 255.1 - диапазон ползунка, не включая 255, т.к. растягиваем только нужное :)
    uint8_t palettepos;         // позиция в массиве указателей паллитр
    uint8_t paletteIdx;         // индекс палитры переданный с UI

    /** флаг, включает использование палитр в эффекте.
     *  влияет на:
     *  - подгрузку дефолтовых палитр при init()
     *  - переключение палитры при изменении ползунка "шкалы"
     *  -  проверку R?
     */
    bool usepalettes=false;
    std::vector<PGMPalette*> palettes;          /**< набор используемых палитр (пустой)*/
    TProgmemRGBPalette16 const *curPalette = nullptr;     /**< указатель на текущую палитру */

    const String &getCtrlVal(int idx) {
        //return (idx<ctrls->size() && idx>=0) ? (*ctrls)[idx]->getVal() : dummy;

        // Добавлена поддержка вариантов следования индексов контролов вида 0,1,2,5,7 т.е. с пропусками
        dummy.clear();
        if(idx<ctrls->size() && idx>=0 && idx<=2 && (*ctrls)[idx]->getId()==idx){
            return (*ctrls)[idx]->getVal();
        } else {
            for(int i = 3; i<ctrls->size(); i++){
                if((*ctrls)[i]->getId()==idx){
                    if(isRandDemo()){
                        dummy = random((*ctrls)[i]->getMin().toInt(),(*ctrls)[i]->getMax().toInt()+1);
                        return dummy;
                    }
                    else
                        return (*ctrls)[i]->getVal();
                }
            }
        }
        return dummy;
    }

public:

    bool isMicOn() {return isMicActive;}

    /** полезные обертки **/
    uint8_t wrapX(int8_t x){ return (x + WIDTH) % WIDTH; }
    uint8_t wrapY(int8_t y){ return (y + HEIGHT) % HEIGHT; }

    EffectCalc(){}

    /**
     * pre_init метод, вызывается отдельно после создания экземпляра эффекта до каких либо иных инициализаций
     * это нужно чтобы объект понимал кто он и возможно было вычитать конфиг для мультиэфектов, никаких иных действий здесь не предполагается
    */
    void pre_init(EFF_ENUM _eff, EffectWorker *_pworker, LList<UIControl *> *_ctrls, LAMPSTATE* _state) {effect = _eff; pworker = _pworker; ctrls = _ctrls; lampstate = _state;}

    /**
     * intit метод, вызывается отдельно после создания экземпляра эффекта для установки базовых переменных
     * в конце выполнения вызывает метод load() который может быть переопределен в дочернем классе
     * @param _eff - энумератор эффекта
     * @param _controls - контролы эффекта
     * @param _state - текущее состояние лампы
     *
    */
    void init(EFF_ENUM _eff, LList<UIControl*>* _controls, LAMPSTATE* _state);

    /**
     * load метод, по умолчанию пустой. Вызывается автоматом из init(), в дочернем классе можно заменять на процедуру первой загрузки эффекта (вместо того что выполняется под флагом load)
     *
    */
    virtual void load();

    /**
     * run метод, Вызывается для прохода одного цикла эффекта, можно переопределять либо фунцией обсчета смого эффекта,
     * либо вспомогательной оберткой, вызывающей приватный метод.
     * Метод должет вернуть true если обсчет эффекта был завершен успешно или false если обсчет был пропущен и кадр не менялся
     * @param ledarr - указатель на массив, пока не используется
     * @param opt - опция, пока не используется, вероятно нужно заменить на какую-нибудь расширяемую структуру
    */
    virtual bool run(CRGB* ledarr, EffectWorker *opt=nullptr);

    /**
     * drynrun метод, всеми любимая затычка-проверка на "пустой" вызов
     * возвращает false если еще не прошло достаточно времени с EFFECTS_RUN_TIMER
     */
    bool dryrun(float n=1.0, uint8_t delay = EFFECTS_RUN_TIMER);

    /**
     * status - статус воркера, если работает и загружен эффект, отдает true
     */
    virtual bool status();

    ///
    /// следующие методы дублируют устранку "яркости", "скорости", "шкалы" для эффекта.
    /// Сейчас это не используется, но соображения "за" следующие:
    ///  - эффекты можно программить со своими локальными переменными, не дергая конкретный
    ///    экземпляр myLamp.effects.getXXX
    ///  - эффекты могут по необходимости масштабировать параметры из байта в свою размерность, или можно расширить базовый класс
    ///  - эфекты могут переопределять методы установки параметров и корректировать их с учетом микрофона, например
    ///


    /**
     * setBrt - установка яркости для воркера
     */
    virtual void setbrt(const byte _brt);

    /**
     * setSpd - установка скорости для воркера
     */
    virtual void setspd(const byte _spd);

    /**
     * setBrt - установка шкалы для воркера
     */
    virtual void setscl(const byte _scl);

    /**
     * setDynCtrl - обработка для динамических контролов idx=3+
     * https://community.alexgyver.ru/threads/wifi-lampa-budilnik-proshivka-firelamp_jeeui-gpl.2739/page-112#post-48848
     */
    virtual String setDynCtrl(UIControl*_val);

    /**
     * загрузка дефолтных палитр в массив и установка текущей палитры
     * в соответствие в "бегунком" шкала/R
     */
    virtual void palettesload();

    /**
     * palletemap - меняет указатель на текущую палитру из набора в соответствие с "ползунком"
     * @param _val - байт "ползунка"
     * @param _pals - набор с палитрами
     */
    virtual void palettemap(std::vector<PGMPalette*> &_pals, const uint8_t _val, const uint8_t _min=1,  const uint8_t _max=255);

    /**
     * метод выбирает текущую палитру '*curPalette' из набора дотупных палитр 'palettes'
     * в соответствии со значением "бегунка" шкалы. В случае если задана паременная rval -
     * метод использует значение R,  иначе используется значение scale
     * (палитры меняются автоматом при изменении значения шкалы/R, метод оставлен для совместимости
     * и для первоначальной загрузки эффекта)
     */
    void scale2pallete();

    /**
     * деструктор по-умолчанию пустой, может быть переопределен
     */
    virtual ~EffectCalc() = default;
    //virtual ~EffectCalc(){ LOG(println,PSTR("DEGUG: Effect was destroyed\n")); } // отладка, можно будет затем закомментировать
};




class EffectWorker {
private:
    LAMPSTATE *lampstate; // ссылка на состояние лампы
    SORT_TYPE effSort; // порядок сортировки в UI
    const uint8_t maxDim = ((WIDTH>HEIGHT)?WIDTH:HEIGHT);

    uint16_t curEff = (uint16_t)EFF_NONE;     ///< энумератор текущего эффекта
    uint16_t selEff = (uint16_t)EFF_NONE;     ///< энумератор выбранного эффекта (для отложенного перехода)
    
    String originalName;    // имя эффекта дефолтное
    String effectName;      // имя эффекта (предварительно заданное или из конфига)
    String soundfile;       // имя/путь к звуковому файлу (DF Player Mini)
    uint8_t version;        // версия эффекта

    LList<EffectListElem*> effects; // список эффектов с флагами из индекса
    LList<UIControl*> controls; // список контроллов текущего эффекта
    LList<UIControl*> selcontrols; // список контроллов выбранного эффекта (пока еще идет фейдер)

    /**
     * создает и инициализирует экземпляр класса выбранного эффекта
     *
    */
    void workerset(uint16_t effect, const bool isCfgProceed = true);

    EffectWorker(const EffectWorker&);  // noncopyable
    EffectWorker& operator=(const EffectWorker&);  // noncopyable

    void clearEffectList(); // очистка списка эффектов, вызываетсяч в initDefault
    void clearControlsList(); // очистка списка контроллов и освобождение памяти

    void effectsReSort(SORT_TYPE st=(SORT_TYPE)(255));

    int loadeffconfig(const uint16_t nb, const char *folder=NULL);

    // получение пути и имени файла конфига эффекта
    const String geteffectpathname(const uint16_t nb, const char *folder=NULL);

    /**
     * проверка на существование "дефолтных" конфигов для всех статичных эффектов
     *
     */
    void chckdefconfigs(const char *folder);

    void savedefaulteffconfig(uint16_t nb, String &filename);
    void saveeffconfig(uint16_t nb, char *folder=NULL);
    void makeIndexFile(const char *folder = NULL);
    // создать или обновить текущий индекс эффекта
    void updateIndexFile();
    // удалить эффект из индексного файла
    void deleteFromIndexFile(const uint16_t effect);

    /**
     * получить версию эффекта из "прошивки" по его ENUM
     */
    const uint8_t geteffcodeversion(const uint8_t id);

    /**
     *  метод загружает и пробует десериализовать джейсон из файла в предоставленный документ,
     *  возвращает true если загрузка и десериализация прошла успешно
     *  @param doc - DynamicJsonDocument куда будет загружен джейсон
     *  @param jsonfile - файл, для загрузки
     */
    bool deserializeFile(DynamicJsonDocument& doc, const char* filepath);

    /**
     * процедура открывает индекс-файл на запись в переданный хендл,
     * возвращает хендл
     */
    File& openIndexFile(File& fhandle, const char *folder);


public:
    std::unique_ptr<EffectCalc> worker = nullptr;           ///< указатель-класс обработчик текущего эффекта
    void initDefault(const char *folder = NULL); // пусть вызывается позже и явно
    ~EffectWorker() { clearEffectList(); clearControlsList(); }

    LList<UIControl*>&getControls() { return isSelected() ? controls : selcontrols; }

    // дефолтный конструктор
    EffectWorker(LAMPSTATE *_lampstate) : effects(), controls(), selcontrols() {
      lampstate = _lampstate;
    /*
      // нельзя вызывать литлфс.бегин из конструктора, т.к. инстанс этого объекта есть в лампе, который декларируется до setup()
      if (!LittleFS.begin()){
          //LOG(println, F("ERROR: Can't mount filesystem!"));
          return;
      }
    */

      for(int8_t id=0;id<3;id++){
        controls.add(new UIControl(
            id,                                     // id
            CONTROL_TYPE::RANGE,                    // type
            id==0 ? String(FPSTR(TINTF_00D)) : id==1 ? String(FPSTR(TINTF_087)) : String(FPSTR(TINTF_088))           // name
        ));
        // selcontrols.add(new UIControl(
        //     id,                                     // id
        //     CONTROL_TYPE::RANGE,                    // type
        //     id==0 ? String(FPSTR(TINTF_00D)) : id==1 ? String(FPSTR(TINTF_087)) : String(FPSTR(TINTF_088)),           // name
        //     String(127),                            // value
        //     String(1),                              // min
        //     String(255),                            // max
        //     String(1)                               // step
        // ));
      }
      //workerset(EFF_NONE);
      selcontrols = controls;
    } // initDefault(); убрал из конструктора, т.к. крайне неудобно становится отлаживать..

    // тип сортировки
    void setEffSortType(SORT_TYPE type) {if(effSort != type) { effectsReSort(type); } effSort = type;}

    // Получить конфиг текущего эффекта
    String geteffconfig(uint16_t nb, uint8_t replaceBright = 0);

    // Получить конфиг эффекта из ФС
    String getfseffconfig(uint16_t nb);

    // конструктор копий эффектов
    EffectWorker(const EffectListElem* base, const EffectListElem* copy);
    // Конструктор для отложенного эффекта
    EffectWorker(uint16_t delayeffnb);
    // конструктор текущего эффекта, для fast=true вычитываетсяч только имя
    EffectWorker(const EffectListElem* eff, bool fast=false);

    // отложенная запись конфига текущего эффекта
    bool autoSaveConfig(bool force=false, bool reset=false);
    // удалить конфиг переданного эффекта
    void removeConfig(const uint16_t nb, const char *folder=NULL);
    // пересоздает индекс с текущего списка эффектов
    void makeIndexFileFromList(const char *folder = NULL);
    // пересоздает индекс с конфигов в ФС
    void makeIndexFileFromFS(const char *fromfolder = NULL, const char *tofolder = NULL);

    byte getModeAmount() {return effects.size();}

    const String &getEffectName() {return effectName;}
    void setEffectName(const String &name, EffectListElem*to) // если текущий, то просто пишем имя, если другой - создаем экземпляр, пишем, удаляем
        {
            if(to->eff_nb==curEff){
                effectName=name;
                saveeffconfig(curEff);
            }
            else {
                EffectWorker *tmp=new EffectWorker(to);
                tmp->curEff=to->eff_nb;
                tmp->selEff=to->eff_nb;
                tmp->setEffectName(name,to);
                tmp->saveeffconfig(to->eff_nb);
                delete tmp;
            }
        }

    const String &getSoundfile() {return soundfile;}
    void setSoundfile(const String &_soundfile, EffectListElem*to) // если текущий, то просто пишем имя звукового файла, если другой - создаем экземпляр, пишем, удаляем
        {if(to->eff_nb==curEff) soundfile=_soundfile; else {EffectWorker *tmp=new EffectWorker(to); tmp->curEff=to->eff_nb; tmp->selEff=to->eff_nb; tmp->setSoundfile(_soundfile,to); tmp->saveeffconfig(to->eff_nb); delete tmp;} }
    const String &getOriginalName() {return originalName;}

    /**
    * вычитать только имя эффекта из конфиг-файла и записать в предоставленную строку
    * в случае отсутствия/повреждения взять имя эффекта из флеш-таблицы, если есть
    * для работы метода не требуется экземпляра класса effectCalc'а
    * @param effectName - String куда записать результат
    * @param nb  - айди эффекта
    * @param folder - какой-то префикс для каталога
    */
    void loadeffname(String& effectName, const uint16_t nb, const char *folder=NULL);

    /**
    * вычитать только имя\путь звука из конфиг-файла и записать в предоставленную строку
    * в случае отсутствия/повреждения возвращает пустую строку
    * @param effectName - String куда записать результат
    * @param nb  - айди эффекта
    * @param folder - какой-то префикс для каталога
    */
    void loadsoundfile(String& effectName, const uint16_t nb, const char *folder=NULL);

    // текущий эффект или его копия
    const uint16_t getEn() {return curEff;}
    //const uint16_t

    // следующий эффект, кроме canBeSelected==false
    uint16_t getNext();
    // предыдущий эффект, кроме canBeSelected==false
    uint16_t getPrev();
    // получить указанный
    uint16_t getBy(uint16_t select){ return select;}
    // перейти по предворительно выбранному

    void moveSelected();
    // перейти на количество шагов, к ближйшему большему (для DEMO)

    void moveByCnt(byte cnt){ uint16_t eff = getByCnt(cnt); directMoveBy(eff); }
    // получить номер эффекта смещенного на количество шагов (для DEMO)
    uint16_t getByCnt(byte cnt);
    // перейти на указанный в обход нормального переключения, использовать только понимая что это (нужно для начальной инициализации и переключений выключенной лампы)
    void directMoveBy(uint16_t select);
    // вернуть первый элемент списка
    EffectListElem *getFirstEffect();
    // вернуть следующий эффект
    EffectListElem *getNextEffect(EffectListElem *current);
    // вернуть выбранный элемент списка
    EffectListElem *getEffect(uint16_t select);
    // вернуть текущий
    uint16_t getCurrent() {return curEff;}
    // вернуть текущий элемент списка
    EffectListElem *getCurrentListElement();
    // вернуть выбранный
    uint16_t getSelected() {return selEff;}
    // вернуть выбранный элемент списка
    EffectListElem *getSelectedListElement();
    void setSelected(const uint16_t effnb);
    bool isSelected(){ return (curEff == selEff); }
    // копирование эффекта
    void copyEffect(const EffectListElem *base);
    // удалить эффект
    void deleteEffect(const EffectListElem *eff, bool isCfgRemove = false);
};
