#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* Установка режима работы секундной точки
 *  Входные параметры:
 *    DOT_MODES dMode: устанавливаемый режим работы секундной точки
 *  Выходные параметры: нет
 */
void dotSetMode(DOT_MODES dMode) {
  // DOT_ALLOWED отключает секундную точку в обычных режимах.
  // В режиме температуры (SHTEMP) точка должна гореть всегда,
  // независимо от пользовательской настройки DOT_ALLOWED.
  if (!DOT_ALLOWED && !(curMode == SHTEMP && dMode == DM_FULL) && !(curMode == SHDATE && dMode == DM_FULL)) dMode = DM_NULL;

  dotMode = dMode;
  dotBrightFlag = false;  
  switch(dMode) {
    case DM_NULL:
      setPWM(DOT, getPWM_CRT(0));
      break;
    case DM_ONCE:
    case DM_HALF:
    case DM_TWICE:
    case DM_THREE:
      dotNumBlink = 0;
      // расчёт шага яркости точки
      dotBrightStep = ((uint16_t)dotMaxBright * 2 * DOT_TIMER + DOT_TIME - 1) / DOT_TIME;   // v2.3.47: ceil() без float
      if (dotBrightStep == 0) dotBrightStep = 1;
      if (dMode == DM_TWICE) dotBrightStep *= 2;
      else if (dMode == DM_THREE) dotBrightStep *= 3;
      break;
    case DM_FULL:
      setPWM(DOT, getPWM_CRT(dotMaxBright));
      break;
  }
}

/* Обеспечение работы секундной точки
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
inline void dotBrightTick() {
  if (dotMode == DM_NULL || dotMode == DM_FULL) return;
  if (dotBrightFlag && dotBrightTimer.isReady()) {
    if (dotBrightDirection) {
      dotBrightCounter += dotBrightStep;
      if (dotBrightCounter >= dotMaxBright) {
        dotBrightDirection = false;
        dotBrightCounter = dotMaxBright;
      }
    } else {
      dotBrightCounter -= dotBrightStep;
      if (dotBrightCounter <= ((dotMode == DM_HALF) ? dotMaxBright / 2 : 0)) {
        dotBrightDirection = true;
        if (dotMode == DM_TWICE || dotMode == DM_THREE) {
          dotNumBlink++;
          if (dotNumBlink == (dotMode == DM_TWICE ? 2 : 3) ) {
            dotNumBlink = 0;
            dotBrightFlag = false;
          }
        } else dotBrightFlag = false;
        dotBrightCounter = ((dotMode == DM_HALF) ? dotMaxBright / 2 : 0);
      }
    }
    setPWM(DOT, getPWM_CRT(dotBrightCounter));
  }
}
