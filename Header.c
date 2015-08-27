/*
htop - Header.c
(C) 2004-2011 Hisham H. Muhammad
Released under the GNU GPL, see the COPYING file
in the source distribution for its full text.
*/

#include "Header.h"

#include "CRT.h"
#include "StringUtils.h"
#include "Platform.h"

#include <assert.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

/*{
#include "Meter.h"
#include "Settings.h"
#include "Vector.h"

typedef struct Header_ {
   Vector** columns;
   Settings* settings;
   struct ProcessList_* pl;
   int nrColumns;
   int pad;
   int height;
} Header;

}*/

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef Header_forEachColumn
#define Header_forEachColumn(this_, i_) for (int i_=0; i_ < this->nrColumns; i_++)
#endif

Header* Header_new(struct ProcessList_* pl, Settings* settings, int nrColumns) {
   Header* this = calloc(1, sizeof(Header));
   this->columns = calloc(nrColumns, sizeof(Vector*));
   this->settings = settings;
   this->pl = pl;
   this->nrColumns = nrColumns;
   Header_forEachColumn(this, i) {
      this->columns[i] = Vector_new(Class(Meter), true, DEFAULT_SIZE);
   }
   return this;
}

void Header_delete(Header* this) {
   Header_forEachColumn(this, i) {
      Vector_delete(this->columns[i]);
   }
   free(this->columns);
   free(this);
}

void Header_populateFromSettings(Header* this) {
   Header_forEachColumn(this, col) {
      MeterColumnSettings* colSettings = &this->settings->columns[col];
      for (int i = 0; i < colSettings->len; i++) {
         Header_addMeterByName(this, colSettings->names[i], col);
         if (colSettings->modes[i] != 0) {
            Header_setMode(this, i, colSettings->modes[i], col);
         }
      }
   }
   Header_calculateHeight(this);
}

void Header_writeBackToSettings(const Header* this) {
   Header_forEachColumn(this, col) {
      MeterColumnSettings* colSettings = &this->settings->columns[col];
      
      String_freeArray(colSettings->names);
      free(colSettings->modes);
      
      Vector* vec = this->columns[col];
      int len = Vector_size(vec);
      
      colSettings->names = calloc(len+1, sizeof(char*));
      colSettings->modes = calloc(len, sizeof(int));
      colSettings->len = len;
      
      for (int i = 0; i < len; i++) {
         Meter* meter = (Meter*) Vector_get(vec, i);
         char* name = calloc(64, sizeof(char*));
         if (meter->param) {
            snprintf(name, 63, "%s(%d)", As_Meter(meter)->name, meter->param);
         } else {
            snprintf(name, 63, "%s", As_Meter(meter)->name);
         }
         colSettings->names[i] = name;
         colSettings->modes[i] = meter->mode;
      }
   }
}

MeterModeId Header_addMeterByName(Header* this, char* name, int column) {
   Vector* meters = this->columns[column];

   char* paren = strchr(name, '(');
   int param = 0;
   if (paren) {
      int ok = sscanf(paren, "(%10d)", &param);
      if (!ok) param = 0;
      *paren = '\0';
   }
   MeterModeId mode = TEXT_METERMODE;
   for (MeterClass** type = Platform_meterTypes; *type; type++) {
      if (String_eq(name, (*type)->name)) {
         Meter* meter = Meter_new(this->pl, param, *type);
         Vector_add(meters, meter);
         mode = meter->mode;
         break;
      }
   }
   return mode;
}

void Header_setMode(Header* this, int i, MeterModeId mode, int column) {
   Vector* meters = this->columns[column];

   if (i >= Vector_size(meters))
      return;
   Meter* meter = (Meter*) Vector_get(meters, i);
   Meter_setMode(meter, mode);
}

Meter* Header_addMeterByClass(Header* this, MeterClass* type, int param, int column) {
   Vector* meters = this->columns[column];

   Meter* meter = Meter_new(this->pl, param, type);
   Vector_add(meters, meter);
   return meter;
}

int Header_size(Header* this, int column) {
   Vector* meters = this->columns[column];
   return Vector_size(meters);
}

char* Header_readMeterName(Header* this, int i, int column) {
   Vector* meters = this->columns[column];
   Meter* meter = (Meter*) Vector_get(meters, i);

   int nameLen = strlen(Meter_name(meter));
   int len = nameLen + 100;
   char* name = malloc(len);
   strncpy(name, Meter_name(meter), nameLen);
   name[nameLen] = '\0';
   if (meter->param)
      snprintf(name + nameLen, len - nameLen, "(%d)", meter->param);

   return name;
}

MeterModeId Header_readMeterMode(Header* this, int i, int column) {
   Vector* meters = this->columns[column];

   Meter* meter = (Meter*) Vector_get(meters, i);
   return meter->mode;
}

void Header_reinit(Header* this) {
   Header_forEachColumn(this, col) {
      for (int i = 0; i < Vector_size(this->columns[col]); i++) {
         Meter* meter = (Meter*) Vector_get(this->columns[col], i);
         if (Meter_initFn(meter))
            Meter_init(meter);
      }
   }
}

/* --- rounding shifts [#cols][corr][col+1]  --- 
 * bars width corrections
 */
static const int HeaderPaddingCorrection[4][4][4] = {
   {{0}},
   {{0,0}},                        // one columns
   {{0,0,0},{0,0,1}},              // two columns
   {{0,0,0,0},{0,0,1,1},{0,0,1,2}} // three columns
};

/* Header layout:
 * --pad--width-- [--ipad--width--]_n --ipad--width--pad--
 * widht = width + rounding correction
 */
void Header_draw(const Header* this) {
   int height = this->height;
   int pad    = this->pad;
   int ipad   = 2 * pad;
   attrset(CRT_colors[RESET_COLOR]);
   for (int y = 0; y < height; y++) {
      mvhline(y, 0, ' ', COLS);
   }
   int width = (COLS - 2 * pad - (this->nrColumns - 1) * ipad) / this->nrColumns;
   int corr  =  COLS - 2 * pad - (this->nrColumns - 1) * ipad - this->nrColumns * width;
   int x = pad;

   Header_forEachColumn(this, col) {
      int wcorr = HeaderPaddingCorrection[this->nrColumns][corr][col+1] - HeaderPaddingCorrection[this->nrColumns][corr][col];
      Vector* meters = this->columns[col];
      for (int y = (pad / 2), i = 0; i < Vector_size(meters); i++) {
         Meter* meter = (Meter*) Vector_get(meters, i);
         meter->draw(meter, x, y, width + wcorr + 1); // +1 <= bar width is w-1 !!!
         y += meter->h;
      }
      x += width + ipad;
   }
}

int Header_calculateHeight(Header* this) {
   int pad = this->settings->headerMargin ? 2 : 0;
   int maxHeight = pad;

   Header_forEachColumn(this, col) {
      Vector* meters = this->columns[col];
      int height = pad;
      for (int i = 0; i < Vector_size(meters); i++) {
         Meter* meter = (Meter*) Vector_get(meters, i);
         height += meter->h;
      }
      maxHeight = MAX(maxHeight, height);
   }
   this->height = maxHeight;
   this->pad = pad;
   return maxHeight;
}
