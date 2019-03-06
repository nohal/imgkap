/*
 *  This source is free writen by M'dJ at 17/05/2011 extended by H.N 2016
 *  As the original author is not available, please post patches and report issues at https://github.com/nohal/imgkap
 *  Use open source FreeImage and gnu gcc
 *  Thank to freeimage, libsb and opencpn
 *
 *    imgkap.c - Convert kap a file from/to a image file and kml to kap
 */

#define VERS   "1.16"

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>        /* for malloc() */
#include <string.h>        /* for strncmp() */

#include <time.h>          /* for date in kap */

#include <FreeImage.h>

/* color type and mask */

typedef union
{
    RGBQUAD  q;
    uint32_t p;
} Color32;

#define RGBMASK 0x00FFFFFF


/* --- Copy this part in header for use imgkap functions in programs and define LIBIMGKAP*/

#define METTERS     0
#define FATHOMS     1
#define FEET        2

#define NORMAL      0
#define OLDKAP      1

#define COLOR_NONE  1
#define COLOR_IMG   2
#define COLOR_MAP   3
#define COLOR_KAP   4

#define FIF_KAP     1024
#define FIF_NO1     1025
#define FIF_TXT     1026
#define FIF_KML     1027


int imgtokap(int typein,char *filein, double lat0, double lon0, int pixpos0x, int pixpos0y, double lat1, double lon1, int pixpos1x, int pixpos1y, int optkap,int color,char *title, int units, char *sd, int optionwgs84, char *optframe, char *fileout, char *gd, char *pr, int optionscale, char* edition, char* updated);
int imgheadertokap(int typein,char *filein,int typeheader,int optkap,int optrc,int color,char *title,char *fileheader,char *fileout);
int kaptoimg(int typein,char *filein,int typeheader,char *fileheader,int typeout,char *fileout,char *optionpal);

int findfiletype(char *file);
int writeimgkap(FILE *out,FIBITMAP **bitmap,int optkap,int colors,Color32 *pal,uint16_t widthin,uint16_t heightin,uint16_t widthout,uint16_t heightout);

int bsb_uncompress_row(int typein, FILE *in, uint8_t *buf_out, uint16_t bits_in,uint16_t bits_out,uint16_t width);
int bsb_compress_row(const uint8_t *buf_in, uint8_t *buf_out, uint16_t bits_out, uint16_t line, uint16_t widthin,uint16_t widthout);

/* --- End copy */


static const double WGSinvf                           = 298.257223563;       /* WGS84 1/f */
static const double WGSexentrik                       = 0.081819;             /* e = 1/WGSinvf; e = sqrt(2*e -e*e) ;*/

struct structlistoption
{
    char const *name;
    int val;
} ;

static struct structlistoption imagetype[] =
{
    {"KAP",FIF_KAP},
    {"NO1",FIF_NO1},
    {"KML",FIF_KML},
    {"TXT",FIF_TXT},
    {NULL, FIF_UNKNOWN},
} ;

static struct structlistoption listoptcolor[] =
{
    {"NONE",COLOR_NONE},
    {"KAP",COLOR_KAP},
    {"IMG",COLOR_IMG},
    {"MAP",COLOR_MAP},
    {NULL,COLOR_NONE}
} ;

int findoptlist(struct structlistoption *liste,char *name)
{
    while (liste->name != NULL)
    {
        if (!strcasecmp(liste->name,name)) return liste->val;
        liste++;
    }
    return liste->val;
}

int findfiletype(char *file)
{
    char *s ;

    s = file + strlen(file)-1;
    while ((s > file) && (*s != '.')) s--;
    s++;
    return findoptlist(imagetype,s);
}

static double postod(double lat0, double lon0, double lat1, double lon1)
{
    double x,v,w;

    lat0 = lat0 * M_PI / 180.;
    lat1 = lat1 * M_PI / 180.;
    lon0 = lon0 * M_PI / 180.;
    lon1 = lon1 * M_PI / 180.;

    v = sin((lon0 - lon1)/2.0);
    w = cos(lat0) * cos(lat1) * v * v;
    x = sin((lat0 - lat1)/2.0);
    return (180. * 60. / M_PI) * 2.0 * asinl(sqrtl(x*x + w));
}

static inline double lontox(double l)
{
    return l*M_PI/180;
}

static inline double lattoy_WS84(double l)
{
    double e = WGSexentrik;
    double s = sinl(l*M_PI/180.0);

    return log(tan(M_PI/4 + l * M_PI/360)*pow((1. - e * s)/(1. + e * s), e/2.));
}


/*------------------ Single Memory algorithm ------------------*/

#define MYBSIZE 1572880

typedef struct smymemory
{
    struct smymemory *next;
    uint32_t size;
} mymemory;

static mymemory *mymemoryfirst = 0;
static mymemory *mymemorycur = 0;

void * myalloc(int size)

{
    void *s = NULL;
    mymemory *mem = mymemorycur;

    if (mem && ((mem->size + size) > MYBSIZE)) mem = 0;

    if (!mem)
    {
        mem = (mymemory *)calloc(MYBSIZE,1);
        if (mem == NULL) return 0;
        mem->size = sizeof(mymemory);

        if (mymemorycur) mymemorycur->next = mem;
        mymemorycur = mem;
        if (!mymemoryfirst) mymemoryfirst = mem;
    }

    s = ((int8_t *)mem + mem->size);
    mem->size += size;
    return s;
}

void myfree(void)

{
    struct smymemory *mem, *next;

    mem = mymemoryfirst;
    while (mem)
    {
        next = mem->next;
        free(mem);
        mem = next;
    }
    mymemoryfirst = 0;
    mymemorycur = 0;
}

/*------------------ Histogram algorithm ------------------*/

typedef struct
{
    Color32 color;
    uint32_t count;
    int16_t num;
} helem;

typedef struct shistogram
{
    Color32 color;
    uint32_t count;
    int16_t num;
    int16_t used;
    struct shistogram *child ;
} histogram;


#define HistIndex2(p,l) ((((p.q.rgbRed >> l) & 0x03) << 4) | (((p.q.rgbGreen >> l) & 0x03) << 2) |    ((p.q.rgbBlue >> l) & 0x03) )
#define HistSize(l) (l?sizeof(histogram):sizeof(helem))
#define HistInc(h,l) (histogram *)(((char *)h)+HistSize(l))
#define HistIndex(h,p,l) (histogram *)((char *)h+HistSize(l)*HistIndex2(p,l))

static histogram *HistAddColor (histogram *h, Color32 pixel )
{
    char level;

    for (level=6;level>=0;level -=2)
    {
        h = HistIndex(h,pixel,level) ;

        if (h->color.p == pixel.p) break;
        if (!h->count && !h->num)
        {
            h->color.p = pixel.p;
            break;
        }
        if (!h->child)
        {
            h->child = (histogram *)myalloc(HistSize(level)*64);
            if (h->child == NULL) return 0;
        }
        h = h->child;
    }

    h->count++;
    return h;
}

static int HistGetColorNum (histogram *h, Color32 pixel)
{
    char level;

    for (level=6;level>=0;level -=2)
    {
        /* 0 < index < 64 */
        h = HistIndex(h,pixel,level) ;
        if (h->color.p == pixel.p) break;
        if (!level)
            break; // erreur
        if (!h->child) break;
        h = h->child;
    }
    if (h->num < 0) return -1-h->num;
    return h->num-1;
}

#define HistColorsCount(h) HistColorsCountLevel(h,6)

static int32_t HistColorsCountLevel (histogram *h,int level)
{
    int i;
    uint32_t count = 0;

    for (i=0;i<64;i++)
    {
        if (h->count) count++;
        if (level)
        {
            if(h->child) count += HistColorsCountLevel(h->child,level-2);
        }
        h = HistInc(h,level);
    }
    return count;
}


/*--------------- reduce begin -------------*/

typedef struct
{
    histogram   *h;

    int32_t     nbin;
    int32_t     nbout;

    int32_t     colorsin;
    int32_t     colorsout;

    int         nextcote;
    int         maxcote;
    int         limcote[8];

    uint64_t    count;
    uint64_t    red;
    uint64_t    green;
    uint64_t    blue;

} reduce;

static inline int HistDist(Color32 a, Color32 b)
{
   int c,r;

   c = a.q.rgbRed - b.q.rgbRed;
   r = c*c;

   c = a.q.rgbGreen - b.q.rgbGreen;
   r += c*c;

   c = a.q.rgbBlue - b.q.rgbBlue;
   r += c*c;

   return sqrt(r);
}

static int HistReduceDist(reduce *r, histogram *h, histogram *e, int cote, int level)
{
    int i;
    int used = 1;
    int curcote;
    int limcote = r->limcote[level];

    for (i=0;i<64;i++)
    {
        if (h->count && !h->num)
        {

            curcote = HistDist((Color32)e->color,(Color32)h->color);

            if (curcote <= cote)
            {
                    uint64_t c;

                    c = h->count;

                    r->count += c;
                    r->red += c * (uint64_t)((Color32)h->color).q.rgbRed ;
                    r->green +=  c * (uint64_t)((Color32)h->color).q.rgbGreen;
                    r->blue +=  c * (uint64_t)((Color32)h->color).q.rgbBlue;

                    h->num = r->nbout;
                    h->count = 0;
                    r->nbin++;
            }
            else
            {
                    if (r->nextcote > curcote)
                        r->nextcote = curcote;
                    used = 0;
            }
        }
        if (level && h->child && !h->used)
        {
            limcote += cote ;

            curcote = HistDist((Color32)e->color,(Color32)h->color);

            if (curcote <= limcote)
                h->used = HistReduceDist(r,h->child,e,cote,level-2);
            if (!h->used)
            {
                if ((curcote > limcote) && (r->nextcote > limcote))
                    r->nextcote = curcote ;
                used = 0;
            }
            limcote -= cote ;
        }
        h = HistInc(h,level);
    }
    return used;
}

static void HistReduceLevel(reduce *r, histogram *h, int level)
{
    int i;

    for (i=0;i<64;i++)
    {
        if (level && h->child && !h->used)
        {
            HistReduceLevel(r, h->child,level-2);
            if (!r->colorsout) break;
        }

        if (h->count && !h->num)
        {
            int32_t cote = 0;
            int32_t nbcolors;
            int32_t curv;

            r->count = r->red = r->green = r->blue = 0;
            r->nbin = 0;
            r->nextcote = 0;
            r->nbout++;

            cote = (int32_t)(pow((double)((1<<24)/(double)r->colorsout),1.0/3.0)/2); //-1;
            r->maxcote = sqrt(3*cote*cote);

            curv = 0;
            nbcolors = (r->colorsin +r->colorsout -1)/r->colorsout;

            while (r->nbin < nbcolors)
            {
                curv += nbcolors - r->nbin;
                cote = (int32_t)(pow(curv,1.0/3.0)/2); // - 1;
                cote = sqrt(3*cote*cote);

                if (r->nextcote > cote)
                    cote = r->nextcote;

                r->nextcote = r->maxcote+1;

                if (cote >= r->maxcote)
                        break;

                h->used = HistReduceDist(r,r->h,h,cote,6);

                if (!r->count)
                {
                    fprintf(stderr,"Erreur quantize\n");
                    return;
                }
            }

            r->colorsin -= r->nbin;
            r->colorsout--;
            {
                histogram *e;
                Color32 pixel ;
                uint64_t c,cc;

                c = r->count; cc = c >> 1 ;
                pixel.q.rgbRed = (uint8_t)((r->red + cc) / c);
                pixel.q.rgbGreen = (uint8_t)((r->green + cc) / c);
                pixel.q.rgbBlue = (uint8_t)((r->blue + cc) / c);
                pixel.q.rgbReserved = 0;

                e = HistAddColor(r->h,pixel);
                e->count = r->count;
                e->num = -r->nbout;

            }

            if (!r->colorsout) break;
        }
        h = HistInc(h,level);
    }

}

static int HistReduce(histogram *h, int colorsin, int colorsout)
{
    reduce r;

    r.h = h;

    r.nbout = 0;

    if (!colorsout || !colorsin) return 0;

    if (colorsout > 0x7FFF) colorsout = 0x7FFF;
    if (colorsout > colorsin) colorsout = colorsin;
    r.colorsin = colorsin;
    r.colorsout = colorsout;

    r.limcote[2] = sqrt(3*3*3) ;
    r.limcote[4] = sqrt(3*15*15) ;
    r.limcote[6] = sqrt(3*63*63) ;

    HistReduceLevel(&r,h,6);

    return r.nbout;
}

/*--------------- reduce end -------------*/

static int _HistGetList(histogram *h,helem **e,int nbcolors,char level)
{
    int i;
    int nb;

    nb = 0;
    for (i=0;i<64;i++)
    {
        if (h->count && (h->num < 0))
        {
            e[-1-h->num] = (helem *)h;
            nb++;
        }

        if (level && h->child) nb += _HistGetList(h->child,e,nbcolors-nb,level-2);
        if (nb > nbcolors)
                return 0;
        h = HistInc(h,level);
    }
    return nb;
}


static int HistGetPalette(uint8_t *colorskap,uint8_t *colors,Color32 *palette,histogram *h,int nbcolors, int nb, int optcolors, Color32 *imgpal,int maxpal)
{
    int i,j;
    helem *t,*e[128];
    uint8_t numpal[128];

    /* get colors used */
    if ((i= _HistGetList(h,e,nbcolors,6)) != nbcolors)
    {
        fprintf(stderr, "Can't process the palette, reduce it before using imgkap.\n");
        return 0;
    }

    /* load all color in final palette */
    memset(numpal,0,sizeof(numpal));
    if (!imgpal)
    {
        for (i=0;i<nbcolors;i++)
        {
            if (!(palette[i].q.rgbReserved & 1)) palette[i].p = e[i]->color.p;
            palette[i].q.rgbReserved |= 1;
            colors[i] = i;
            numpal[i] = i;
        }
        palette->q.rgbReserved |= 8;
        maxpal = nbcolors;
    }
    else
    {
        for (i=maxpal-1;i>=0;i--)
        {
            j = HistGetColorNum(h,imgpal[i]);
            if (j>=0)
            {
                if (!(palette[i].q.rgbReserved & 1))
                    palette[i].p = imgpal[i].p;
                palette[i].q.rgbReserved |= 1;
                numpal[j] = i;
                colors[i] = j;
            }
        }
        palette->q.rgbReserved |= 8;
    }

    /* sort palette desc count */
    for (i=0;i<nbcolors;i++)
    {
        for (j=i+1;j<nbcolors;j++)
            if (e[j]->count > e[i]->count)
            {
                t =  e[i];
                e[i] = e[j];
                e[j] = t;
            }
    }
    /* if palkap 0 put first in last */
    if (nb)
    {
        nb=1;
        t =  e[0];
        e[0] = e[nbcolors-1];
        e[nbcolors-1] = t;
    }

    /* get kap palette colors */
    colorskap[0] = 0;
    for (i=0;i<nbcolors;i++)
        colorskap[i+nb] = numpal[-1-e[i]->num];

    /* get num colors in kap palette */
    for (i=0;i<maxpal;i++)
    {
        for (j=0;j<nbcolors;j++)
            if (colors[i] == (-1 - e[j]->num))
                break;
        colors[i] = j+nb;
    }

    /* taitement img && map sur colorskap */
    if ((optcolors == COLOR_IMG) || (optcolors == COLOR_MAP))
    {
        for(i=0;i<maxpal;i++)
        {

            palette[256+i].q.rgbRed = palette[i].q.rgbRed ;
            palette[256+i].q.rgbGreen = palette[i].q.rgbGreen ;
            palette[256+i].q.rgbBlue = palette[i].q.rgbBlue ;
            palette[512+i].q.rgbRed = (palette[i].q.rgbRed)/2 ;
            palette[512+i].q.rgbGreen = (palette[i].q.rgbGreen)/2 ;
            palette[512+i].q.rgbBlue = (palette[i].q.rgbBlue)/2 ;
            palette[768+i].q.rgbRed = (palette[i].q.rgbRed)/4 ;
            palette[768+i].q.rgbGreen = (palette[i].q.rgbGreen)/4 ;
            palette[768+i].q.rgbBlue = (palette[i].q.rgbBlue)/4 ;
            palette[768+i].q.rgbReserved = palette[512+i].q.rgbReserved = palette[256+i].q.rgbReserved = palette[i].q.rgbReserved ;
        }

        if ((optcolors == COLOR_MAP) && (nbcolors < 64))
        {
            Color32 *p = palette+768;

            for(i=0;i<maxpal;i++)
            {
                if ((p->q.rgbRed <= 4) && (p->q.rgbGreen <= 4) && (p->q.rgbBlue <= 4))
                    p->q.rgbRed = p->q.rgbGreen = p->q.rgbBlue = 55;

                if ((p->q.rgbRed >= 60) && (p->q.rgbGreen >= 60) && (p->q.rgbBlue >= 60))
                    p->q.rgbRed = p->q.rgbGreen = p->q.rgbBlue = 0;
                p++;
            }


        }
    }
/*
    for (i=0;i<nbcolors;i++)
    {
        printf("eorder %d rgb %d %d %d\n",i,e[i]->color.q.rgbRed,e[i]->color.q.rgbGreen,e[i]->color.q.rgbBlue);
    }
    for (i=0;i<maxpal;i++)
    {
        printf("palette %d rgb %d %d %d\n",i,palette[i].q.rgbRed,palette[i].q.rgbGreen,palette[i].q.rgbBlue);
    }
    for (i=0;i<nbcolors+nb;i++)
    {
        j = colorskap[i];
        printf("palkap %d rgb %d %d %d\n",i,palette[j].q.rgbRed,palette[j].q.rgbGreen,palette[j].q.rgbBlue);
    }
    for (i=0;i<maxpal;i++)
    {
        printf("indexcol %i colors : %d\n",i,colors[i]);
    }
*/
    nbcolors += nb;
    return nbcolors;
}

#define HistFree(h) myfree()


/*------------------ End of Histogram ------------------*/

typedef struct shsv
{
    double hue;
    double sat;
    double val;
} HSV;


/* read in kap file */

//* si size < 0 lit jusqu'a \n (elimine \r) et convertie NO1 int r = (c - 9) & 0xFF; */

static inline int fgetkapc(int typein, FILE *in)
{
    int c;


    c = getc(in);
    if (typein == FIF_NO1) return (c - 9) & 0xff;
    return c;
}

static int fgetkaps(char *s, int size, FILE *in, int typein)
{
    int i,c;

    i = 0;
    while (((c = getc(in)) != EOF) && size)
    {
        if (typein == FIF_NO1) c = (c - 9) & 0xff ;
        if (size > 0)
        {
            s[i++] = (char)c ;
            size--;
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n')
        {
            s[i] = 0;
            size++;
            break;
        }
        s[i++] = (char)c;
        size++;
        if (!c) break;
    }
    return i;
}

/* function read and write kap index */

static int bsb_write_index(FILE *fp, uint16_t height, uint32_t *index)
{
    uint8_t l;

        /* Write index table */
        while (height--)
        {
            /* Indices must be written as big-endian */
            l = (*index >> 24) & 0xff;
            fputc(l, fp);
            l = (*index >> 16) & 0xff;
            fputc(l, fp);
            l = (*index >> 8) & 0xff;
            fputc(l, fp);
            l = *index & 0xff;
            fputc(l, fp);
            index++;
        }
        return 1;
}

static uint32_t *bsb_read_index(int typein,FILE *in,uint16_t height)
{
    uint32_t l,end;
    uint32_t *index;
    int i;

    index = NULL;

    fseek(in,-4,SEEK_END);
    end = ftell(in);

    l = ((uint32_t)fgetkapc(typein,in)<<24)+((uint32_t)fgetkapc(typein,in)<<16)+((uint32_t)fgetkapc(typein,in)<<8)+((uint32_t)fgetkapc(typein,in));

    if (((end - l)/4) != height) return NULL;

    index = (uint32_t *)malloc(height*sizeof(uint32_t));
    if (index == NULL) return NULL;


    fseek(in,l,SEEK_SET);

    /* Read index table */
    for (i=0; i < height; i++)
    {
        index[i] = ((uint32_t)fgetkapc(typein,in)<<24)+((uint32_t)fgetkapc(typein,in)<<16)+((uint32_t)fgetkapc(typein,in)<<8)+((uint32_t)fgetkapc(typein,in));
    }
    return index;
}

/* bsb compress number, not value 0 at first write */

static uint16_t bsb_compress_nb(uint8_t *p, uint16_t nb, uint8_t pixel, uint16_t max)
{
    uint16_t count = 0;

    if (nb > max)
    {
        count = bsb_compress_nb(p,nb>>7,pixel|0x80,max);
        p[count++] = (nb & 0x7F) | (pixel & 0x80);
        return count;
    }
    pixel |= nb ;
    if (!pixel) p[count++] = 0x80;
    p[count++] = pixel ;
    return count;
}


/* write line bsb */

int bsb_compress_row(const uint8_t *buf_in, uint8_t *buf_out, uint16_t bits_out, uint16_t line, uint16_t widthin, uint16_t widthout)
{
    uint16_t    ibuf,run_length ;
    uint16_t    ipixelin,ipixelout,xout;
    uint8_t        last_pix;
    uint16_t    dec, max;

    dec = 7-bits_out;
    max = (1<<dec) -1;

    /*      write the line number */
    ibuf = bsb_compress_nb(buf_out,line,0,0x7F);

    ipixelin = ipixelout = 0;

    while ( ipixelin < widthin )
    {
        last_pix = buf_in[ipixelin];
        ipixelin++;
        ipixelout++;

        /* Count length of same pixel */
        run_length = 0;
        if (ipixelin == 1592)
            ipixelin = 1592;
        while ( (ipixelin < widthin) && (buf_in[ipixelin] == last_pix) )
        {
            ipixelin++;
            ipixelout++;
            run_length++;
        }

        /* Extend, like but faster (total time/2) than xout = round((double)ipixelin*widthout/widthin); */
        xout = ((uint32_t)((ipixelin<<1)+1)*widthout)/((uint32_t)widthin<<1);
        if (xout > ipixelout)
        {
            run_length += xout - ipixelout;
            ipixelout = xout;
        }

        /* write pixel*/
        ibuf += bsb_compress_nb(buf_out+ibuf,run_length,last_pix<<dec,max);
    }
    buf_out[ibuf++] = 0;
    return ibuf;
}

/* bsb uncompress number */

static uint16_t bsb_uncompress_nb(int typein,FILE *in, uint8_t *pixel, uint8_t decin, uint8_t maxin)
{
    uint8_t c;
    uint16_t count;

    c = fgetkapc(typein,in);

    count = (c & 0x7f);
    *pixel = count >> decin;
    count &= maxin;
    while (c & 0x80)
    {
        c = fgetkapc(typein,in);
        count = (count << 7) + (c & 0x7f);
    }
    return count+1;
}

/* read line bsb */

int bsb_uncompress_row(int typein, FILE *in, uint8_t *buf_out, uint16_t bits_in,uint16_t bits_out, uint16_t width)
{
    uint16_t    count;
    uint8_t     pixel;
    uint8_t     decin, maxin;
    uint16_t    xout = 0;

    decin = 7-bits_in;
    maxin = (1<<decin) - 1;

    /* read the line number */
    count = bsb_uncompress_nb(typein, in,&pixel,0,0x7F);

    /* no test count = line number */
    switch (bits_out)
    {
        case 1:
            while ( width )
            {
                count = bsb_uncompress_nb(typein,in,&pixel, decin,maxin);
                if (count > width) count = width;
                width -= count;
                while (count)
                {
                    buf_out[xout>>3] |= pixel<<(7-(xout&0x7));
                    xout++;
                    count--;
                }
            }
            break;
        case 4:
             while ( width )
             {
                count = bsb_uncompress_nb(typein,in,&pixel, decin,maxin);
                if (count > width) count = width;
                width -= count;
                while (count)
                {
                    buf_out[xout>>1] |= pixel<<(4-((xout&1)<<2));
                    xout++;
                    count--;
                }
            }
            break;
        case 8:
            while ( width )
            {
                count = bsb_uncompress_nb(typein,in,&pixel, decin,maxin);
                if (count > width) count = width;
                width -= count;
                while (count)
                {
                    *buf_out++ = pixel;
                    count--;
                }
            }
            break;
    }
    /* read last byte (0) */
    getc(in);
    return 0;
}

static void read_line(uint8_t *in, uint16_t bits, int width, uint8_t *colors, histogram *hist, uint8_t *out)
{
    int i;
    uint8_t c = 0;

    switch (bits)
    {
        case 1:
            for (i=0;i<width;i++)
            {
                out[i] = colors[(in[i>>3] >> (7-(i & 0x7))) & 1];
            }
            return;
        case 4:
            for (i=0;i<width;i++)
            {
                c = in[i >> 1];
                out[i++] = colors[(c>>4) & 0xF];
                out[i] = colors[c & 0xF];
            }
            return;
        case 8:
            for (i=0;i<width;i++)
            {
                out[i] = colors[in[i]];
            }
            return;
        case 24:
        {
            Color32 cur, last;
            last.p = 0xFFFFFFFF;

            for (i=0;i<width;i++)
            {
                cur.p = ( *(uint32_t *)in & RGBMASK);
                if (last.p != cur.p)
                {
                    c = colors[HistGetColorNum(hist, cur)];
                    last = cur;
                }
                out[i] = c;
                in += 3;
            }
        }
    }
}


static uint32_t GetHistogram(FIBITMAP *bitmap,uint32_t bits,uint16_t width,uint16_t height,Color32 *pal,histogram *hist)
{
    uint32_t    i,j;
    Color32     cur;
    uint8_t     *line,k;
    histogram   *h = hist;

    switch (bits)
    {
        case 1:
            HistAddColor (hist, pal[0]);
            h = HistAddColor (hist, pal[1]);
            h->count++;
            break;

        case 4:
            for (i=0;i<height;i++)
            {
                line = FreeImage_GetScanLine(bitmap, i);
                cur.p = (width+1)>>1;
                for (j=0;j<cur.p;j++)
                {
                    k = (*line++)>>4;
                    if (h->color.p == pal[k].p)
                    {
                        h->count++;
                        continue;
                    }
                    h = HistAddColor (hist, pal[k]);
                }
                line = FreeImage_GetScanLine(bitmap, i);
                cur.p = width >> 1;
                for (j=0;j<cur.p;j++)
                {
                    k = (*line++)&0x0F;
                    if (h->color.p == pal[k].p)
                    {
                        h->count++;
                        continue;
                    }
                    h = HistAddColor (hist, pal[k]);
                }
            }
            break;

        case 8:
            for (i=0;i<height;i++)
            {
                line = FreeImage_GetScanLine(bitmap, i);
                for (j=0;j<width;j++)
                {
                    k = *line++ ;
                    if (h->color.p == pal[k].p)
                    {
                        h->count++;
                        continue;
                    }
                    h = HistAddColor (hist, pal[k]);
                }
            }
            break;

        case 24:
            for (i=0;i<height;i++)
            {
                line = FreeImage_GetScanLine(bitmap, i);
                for (j=0;j<width;j++)
                {
                    cur.p = *(uint32_t *)(line) & RGBMASK;
                    line += 3;
                    if (h->color.p == cur.p)
                    {
                        h->count++;
                        continue;
                    }
                    h = HistAddColor (hist, cur);
                }
            }
            break;
    }

    return HistColorsCount(hist);
}

static const char *colortype[] = {"RGB","DAY","DSK","NGT","NGR","GRY","PRC","PRG"};

int writeimgkap(FILE *out,FIBITMAP **bitmap,int optkap, int optcolors, Color32 *palette, uint16_t widthin,uint16_t heightin, uint16_t widthout, uint16_t heightout)
{
    uint16_t    i,cpt,len,cur,last;
    int         num_colors;

    uint32_t    *index;
    uint8_t     *buf_in,*buf_out;
    int         bits_in,bits_out;

    uint8_t     colorskap[128];
    uint8_t     colors[256];
    histogram   hist[64];

    memset(hist,0,sizeof(hist));
    memset(colors,0,sizeof(colors));
    memset(colorskap,0,sizeof(colorskap));

    bits_in = FreeImage_GetBPP(*bitmap);

    /* make bitmap 24, accept only 1 4 8 24 bits */
    if ((bits_in > 8) && (bits_in != 24))
    {
        FIBITMAP *bitmap24;

        bitmap24 = FreeImage_ConvertTo24Bits(*bitmap);
        if (bitmap24 == NULL)
        {
            fprintf(stderr,"ERROR - bitmap PPP is incorrect\n");
            return 2;
        }
        FreeImage_Unload(*bitmap);
        *bitmap = bitmap24;
        bits_in = 24;
    }

    /* read histogram */
    num_colors = GetHistogram(*bitmap,bits_in,widthin,heightin,(Color32 *)FreeImage_GetPalette(*bitmap),hist);
    if (!num_colors)
    {
        fprintf(stderr,"ERROR - No Colors or bitmap bits %d\n",num_colors);
        return 2;
    }

    /* reduce colors */
    num_colors = HistReduce(hist,num_colors,(optkap == NORMAL)?128:127);

    bits_out = ceil(log2(num_colors + ((optkap == NORMAL)?0:1)));

    /* if possible do not use colors 0 */
    len = ((1<<bits_out) > num_colors)?1:0;

    if (optcolors != COLOR_KAP )
        memset(palette,0,sizeof(Color32)*256*8);

    /* sort palette with 0 blank and get index */
    num_colors = HistGetPalette(colorskap,colors,palette,hist,num_colors,len,optcolors,(Color32 *)FreeImage_GetPalette(*bitmap),FreeImage_GetColorsUsed(*bitmap));
    if (!num_colors)
    {
        fprintf(stderr,"ERROR - internal GetPalette\n");
        return 2;
    }

    fputs("OST/1\r\n", out);
    fprintf(out, "IFM/%d\r\n",bits_out);

    /* Write RGB tags for colormap */
    for (cpt=0;cpt<8;cpt++)
    {
        if (! palette[256*cpt].q.rgbReserved) continue;
        for (i = len; i < num_colors; i++)
        {
                fprintf(out, "%s/%d,%d,%d,%d\r\n",
                    colortype[cpt],
                    i,
                    palette[256*cpt+colorskap[i]].q.rgbRed,
                    palette[256*cpt+colorskap[i]].q.rgbGreen,
                    palette[256*cpt+colorskap[i]].q.rgbBlue
                    );
        }
    }

    fputc(0x1a, out);
    fputc('\0', out);
    fputc(bits_out, out);



    buf_in = (uint8_t *)malloc((widthin + 4)/4*4);
    /* max space bsb encoded line can take */
    buf_out = (uint8_t *)malloc((widthout*2 + 8)/4*4);
    index = (uint32_t *)malloc((heightout + 1) * sizeof(uint32_t));
    if ((buf_in == NULL) || (buf_out == NULL) || (index == NULL))
    {
        fprintf(stderr,"ERROR - mem malloc\n");
        return 2;
    }

    last = -1;
    for (i = 0; i<heightout; i++)
    {
        /* Extend on height */
        cur = round((double)i * heightin / heightout);
        if (cur != last)
        {
            last = cur;
            read_line(FreeImage_GetScanLine(*bitmap, heightin-cur-1), bits_in, widthin, colors, hist,buf_in);
        }

        /* Compress raster and write to BSB file */

        len = bsb_compress_row(buf_in, buf_out, bits_out, i, widthin,widthout);

        /* Record index table */
        index[i] = ftell(out);

        /* write data*/
        fwrite(buf_out, len, 1, out);
    }

    free(buf_in);
    free(buf_out);
    HistFree(hist);

    /* record start-of-index-table file tion in the index table */
    index[heightout] = ftell(out);

    bsb_write_index(out, heightout+1, index);

    free(index);
    return 0;
}


static int readkapheader(int typein,FILE *in,int typeout, FILE *out, char *date,char *title,int optcolor,int *widthout, int *heightout,  double *rx, double *ry, int *depth, RGBQUAD *palette)

{
    char    *s;
    int     result =  0;
    char    line[1024];

    /* lit l entete kap  y compris RGB et l'écrit dans un fichier sauf RGB*/
    *widthout = *heightout = 0;
    if (depth != NULL) *depth = 0;
    if (palette != NULL) memset(palette,0,sizeof(RGBQUAD)*128);

    while (fgetkaps(line,-1024,in,typein) > 0)
    {
        if (line[0] == 0x1a)
            break;
        if ((s = strstr(line, "RA=")))
        {
            unsigned x0, y0;

            /* Attempt to read old-style NOS (4 parameter) version of RA= */
            /* then fall back to newer 2-argument version */
            if ((sscanf(s,"RA=%d,%d,%d,%d",&x0,&y0,widthout,heightout)!=4) &&
                (sscanf(s,"RA=%d,%d", widthout, heightout) != 2))
            {
                result = 1;
                break;
            }
        }
        if (palette != NULL)
        {
            int index,r,g,b;
            RGBQUAD *p = NULL;

            if (sscanf(line, "RGB/%d,%d,%d,%d", &index, &r, &g, &b) == 4) p = palette;
            if (sscanf(line, "DAY/%d,%d,%d,%d", &index, &r, &g, &b) == 4) p = palette+256;
            if (sscanf(line, "DSK/%d,%d,%d,%d", &index, &r, &g, &b) == 4) p = palette+256*2;
            if (sscanf(line, "NGT/%d,%d,%d,%d", &index, &r, &g, &b) == 4) p = palette+256*3;
            if (sscanf(line, "NGR/%d,%d,%d,%d", &index, &r, &g, &b) == 4) p = palette+256*4;
            if (sscanf(line, "GRY/%d,%d,%d,%d", &index, &r, &g, &b) == 4) p = palette+256*5;
            if (sscanf(line, "PRC/%d,%d,%d,%d", &index, &r, &g, &b) == 4) p = palette+256*6;
            if (sscanf(line, "PRG/%d,%d,%d,%d", &index, &r, &g, &b) == 4) p = palette+256*7;

            if (p != NULL)
            {
                if ((unsigned)index > 127)
                {
                    result = 2;
                    break;
                }
                p[0].rgbReserved |= 8;
                p[index].rgbReserved |= 1;
                p[index].rgbRed = r;
                p[index].rgbGreen = g;
                p[index].rgbBlue = b;
            }
        }
        if (depth != NULL) sscanf(line, "IFM/%d", depth);

        if ( (rx != NULL) && (s = strstr(line, "DX=")) )
            sscanf(s, "DX=%lf", rx);
        if ( (ry != NULL) && (s = strstr(line, "DY=")) )
            sscanf(s, "DY=%lf", ry);

        if ((out != NULL) && (typeout != FIF_UNKNOWN))
        {
            if (typeout != FIF_TXT)
            {
                if (!strncmp(line,"RGB/",4)) continue;
                if (!strncmp(line,"DAY/",4)) continue;
                if (!strncmp(line,"DSK/",4)) continue;
                if (!strncmp(line,"NGT/",4)) continue;
                if (!strncmp(line,"NGR/",4)) continue;
                if (!strncmp(line,"GRY/",4)) continue;
                if (!strncmp(line,"PRC/",4)) continue;
                if (!strncmp(line,"PRG/",4)) continue;
            }

            if ((*line == '!') && strstr(line,"M'dJ")) continue;
            if ((*line == '!') && strstr(line,"imgkap")) continue;

            if (!strncmp(line,"VER/",4) && ((optcolor == COLOR_IMG) || (optcolor == COLOR_MAP)))
            {
                fprintf(out,"VER/3.0\r\n");
                continue;
            }

            if (!strncmp(line,"OST/",4)) continue;
            if (!strncmp(line,"IFM/",4)) continue;

            if ((s = strstr(line, "ED=")) && (date != NULL))
            {
                *s = 0;
                while (*s && (*s != ',')) s++;
                fprintf(out,"%sED=%s%s\r\n",line,date,s);
                continue;

            }
            if ((s = strstr(line, "NA=")) && (title != NULL) && *title)
            {
                *s = 0;
                while (*s && (*s != ',')) s++;
                fprintf(out,"%sNA=%s%s\r\n",line,title,s);
                continue;
            }
            fprintf(out,"%s\r\n",line);
        }
    }
    return result;
}

int kaptoimg(int typein,char *filein,int typeheader,char *fileheader,int typeout, char *fileout, char *optionpal)

{
    int result;
    int cpt, width, height;
    int bits_in,bits_out;
    RGBQUAD palette[256*8];
    RGBQUAD *bitmappal;
    FIBITMAP *bitmap;
    FILE *in, *header = NULL ;
    header = NULL;
    double rx,ry;
    uint8_t *line = NULL;
    uint32_t *index;

    memset(palette,0,sizeof(palette));

    if (optionpal && !strcasecmp(optionpal,"ALL") && (typeout != (int)FIF_TIFF) && (typeout != (int)FIF_GIF))
    {
        typeout = FIF_TIFF;

        fprintf(stderr,"ERROR - Palette ALL accepted with only TIF or GIF %s\n",fileout);
        return 2;
    }

    in = fopen(filein, "rb");
    if (in == NULL)
    {
        fprintf(stderr,"ERROR - Can't open KAP file %s\n",filein);
        return 2;
    }
    if (fileheader != NULL)
    {
        header = fopen(fileheader, "wb");
        if (header == NULL)
        {
            fprintf(stderr,"ERROR - Can't create header KAP file %s\n",fileheader);
            fclose(in);
            return 2;
        }
    }
    if (typeheader == FIF_KAP)
        typeheader = FIF_TXT;
    result = readkapheader(typein,in,typeheader,header,NULL,NULL,COLOR_NONE,&width,&height,&rx,&ry,&bits_in,palette);
    if (header != NULL) fclose(header);
    if (result)
    {

        fprintf(stderr,"ERROR - Invalid Header file %s\n",fileheader);
        fclose(in);
        return result;
    }
    if ((fileout == NULL) || !*fileout) return 0;

    bits_out = bits_in;
    if (bits_in > 1)
    {
        if (bits_in > 4)  bits_out = 8;
        else bits_out = 4;
    }

    if ((typeout != (int)FIF_TIFF) && (typeout != (int)FIF_ICO) && (typeout != (int)FIF_PNG) && (typeout != (int)FIF_BMP))
        bits_out = 8;

    index = bsb_read_index(typein,in,height);
    if (index == NULL)
    {
        fprintf(stderr,"ERROR - Invalid index table in %s\n",fileheader);
        fclose(in);
        return 3;
    }

    /* Create bitmap */

    bitmap = FreeImage_AllocateEx(width, height, bits_out,palette,FI_COLOR_IS_RGB_COLOR,palette,0,0,0);
    bitmappal = FreeImage_GetPalette(bitmap);

/* a revoir
    FreeImage_SetDotsPerMeterX(bitmap,rx);
    FreeImage_SetDotsPerMeterY(bitmap,ry);
*/

    /* uncompress and write kap image into bitmap */
    for (cpt=0;cpt<height;cpt++)
    {
        fseek(in,index[cpt],SEEK_SET);
        line = FreeImage_GetScanLine(bitmap,height-cpt-1);
        bsb_uncompress_row (typein,in,line,bits_in,bits_out,width);
    }

    free(index);
    fclose(in);

    cpt = 0;
    if (optionpal)
    {
        cpt = -2;
        if (!strcasecmp(optionpal,"ALL")) cpt = -1;
        if (!strcasecmp(optionpal,"RGB")) cpt = 0;
        if (!strcasecmp(optionpal,"DAY")) cpt = 1;
        if (!strcasecmp(optionpal,"DSK")) cpt = 2;
        if (!strcasecmp(optionpal,"NGT")) cpt = 3;
        if (!strcasecmp(optionpal,"NGR")) cpt = 4;
        if (!strcasecmp(optionpal,"GRY")) cpt = 5;
        if (!strcasecmp(optionpal,"PRC")) cpt = 6;
        if (!strcasecmp(optionpal,"PRG")) cpt = 7;
        if (cpt == -2)
        {
            fprintf(stderr,"ERROR - Palette %s not exist in %s\n",optionpal,filein);
            FreeImage_Unload(bitmap);
            return 2;
        }
    }
    if (cpt >= 0)
    {
        if (cpt > 0)
        {
            RGBQUAD *pal = palette + 256*cpt;
            if (!pal->rgbReserved)
            {
                fprintf(stderr,"ERROR - Palette %s not exist in %s\n",optionpal,filein);
                FreeImage_Unload(bitmap);
                return 2;
            }
            for (result=0;result<256;result++) pal[result].rgbReserved = 0;
            memcpy(bitmappal,pal,sizeof(RGBQUAD)*256);
        }

        result = FreeImage_Save((FREE_IMAGE_FORMAT)typeout,bitmap,fileout,0);
    }
    else
    {
        FIMULTIBITMAP *multi;
        RGBQUAD *pal;

        multi = FreeImage_OpenMultiBitmap((FREE_IMAGE_FORMAT)typeout,fileout,TRUE,FALSE,TRUE,0);
        if (multi == NULL)
        {
            fprintf(stderr,"ERROR - Alloc multi bitmap\n");
            FreeImage_Unload(bitmap);
            return 2;
        }
        for (cpt=0;cpt<8;cpt++)
        {
            pal = palette + 256*cpt;
            if (pal->rgbReserved)
            {
                for (result=0;result<256;result++) pal[result].rgbReserved = 0;
                memcpy(bitmappal,pal,sizeof(RGBQUAD)*256);
                FreeImage_AppendPage(multi,bitmap);
            }
        }
        FreeImage_CloseMultiBitmap(multi,0);
        result = 1;
    }

    FreeImage_Unload(bitmap);

    return !result;
}


int imgheadertokap(int typein,char *filein,int typeheader, int optkap, int optionrc, int color, char *title, char *fileheader,char *fileout)
{
    int         widthin,heightin,widthout,heightout;
    int         bits_in,bits_out;
    int         result;
    RGBQUAD     palette[256*8];
    FIBITMAP    *bitmap = 0;
    FIBITMAP    *tmp_bitmap = 0;
    FILE        *out;
    FILE        *header;
    char        datej[20];


    memset(palette,0,sizeof(palette));
    widthin = heightin = 0;

    /* Read image file */
    if (typein != FIF_KAP)
    {
    	tmp_bitmap = FreeImage_Load((FREE_IMAGE_FORMAT)typein,filein,BMP_DEFAULT);
        if (tmp_bitmap == NULL)
        {
            fprintf(stderr, "ERROR - Could not open or error in image file\"%s\"\n", filein);
            return 2;
        }
        widthin = FreeImage_GetWidth(tmp_bitmap);
        heightin = FreeImage_GetHeight(tmp_bitmap);
        if (!widthin || !heightin)
        {
            fprintf(stderr, "ERROR - Invalid image size (width=%d,height=%d)\n", widthin, heightin);
            FreeImage_Unload(tmp_bitmap);
            return 2;
        }

        if(optionrc == 1)
        {
        	/* reduce number of colors to 127  */
        	bitmap = FreeImage_ColorQuantizeEx(tmp_bitmap, FIQ_NNQUANT, 127, 0, NULL);
        	FreeImage_Unload(tmp_bitmap);
        }
        else
        {
        	bitmap = tmp_bitmap;
        }

    }

    out = fopen(fileout, "wb");
    if (out == NULL)
    {
        fprintf(stderr,"ERROR - Can't open KAP file %s\n",fileout);
        if (bitmap) FreeImage_Unload(bitmap);
        return 2;
    }

    /* Read date */
    {
        time_t      t;
        struct tm   *date;

        time(&t) ;
        date = localtime(&t);
        strftime(datej, sizeof(datej), "%d/%m/%Y",date);
    }

    header = fopen(fileheader, "rb");
    if (header == NULL)
    {
        fprintf(stderr,"ERROR - Can't open Header file %s\n",fileheader);
        if (bitmap) FreeImage_Unload(bitmap);
        fclose(out);
        return 2;
    }

    result = 1;
    if ((typeheader == FIF_TXT) || (typeheader == FIF_KAP))
    {
        /* Header comment file outut */
        char *s;

        for (s=filein+strlen(filein)-1;s>filein;s--)
            if ((*s == '\\') || (*s =='/'))
            {
                s++;
                break;
            }

        fprintf(out,"! 2011 imgkap %s - at %s from %.35s\r\n", VERS,datej,s);

        result = readkapheader(typeheader,header,FIF_KAP,out,datej,title,color, &widthout, &heightout,NULL,NULL,&bits_in,palette);
    }

    if (result)
    {
        fprintf(stderr,"ERROR - Invalid Header type %s\n",fileheader);
        if (bitmap)
            FreeImage_Unload(bitmap);
        fclose(header);
        fclose(out);
        return 2;
    }

    if (typein == FIF_KAP)
    {
        int cpt;
        uint8_t *line = NULL;
        uint32_t *index;

        widthin = widthout;
        heightin = heightout;
        bits_out = bits_in;
        if (bits_in > 1)
        {
            if (bits_in > 4)  bits_out = 8;
            else bits_out = 4;
        }

        index = bsb_read_index(typein,header,heightin);
        if (index == NULL)
        {
            fprintf(stderr,"ERROR - Invalid index table in %s\n",fileheader);
            fclose(header);
            fclose(out);
            return 3;
        }

        /* Create bitmap */
        bitmap = FreeImage_AllocateEx(widthin, heightin, bits_out,palette,FI_COLOR_IS_RGB_COLOR,palette,0,0,0);

        /* uncompress and write kap image into bitmap */
        for (cpt=0;cpt<heightin;cpt++)
        {
            fseek(header,index[cpt],SEEK_SET);
            line = FreeImage_GetScanLine(bitmap,heightin-cpt-1);
            bsb_uncompress_row (typein,header,line,bits_in,bits_out,widthin);
        }

        free(index);
    }
    fclose(header);

    if ((widthin > widthout) || (heightin > heightout))
    {
        fprintf(stderr, "ERROR - Image input is greater than output width=%d -> %d,height=%d -> %d \n", widthin,widthout, heightin,heightout);
        FreeImage_Unload(bitmap);
        fclose(out);
        return 2;
    }

    if (((widthout*10/widthin) > 11) || ((heightout*10/heightin) > 11))
    {
        fprintf(stderr, "ERROR - Image input is too smaller than output width=%d -> %d,height=%d -> %d \n", widthin,widthout, heightin,heightout);
        FreeImage_Unload(bitmap);
        fclose(out);
        return 2;
    }

    result = writeimgkap(out,&bitmap,optkap,color,(Color32 *)palette,widthin,heightin,widthout,heightout);

    FreeImage_Unload(bitmap);
    fclose(out);

    return result;
}

int imgtokap(int typein,char *filein, double lat0, double lon0, int pixpos0x, int pixpos0y, double lat1, double lon1, int pixpos1x, int pixpos1y, int optkap, int color, char *title, int units, char *sd, int optionwgs84, char *optframe, char *fileout, char *gd, char *pr, int optionscale, char* edition, char* updated)
{
    uint16_t    dpi,widthout,heightout,widthoutr,heightoutr;
    uint32_t    widthin,heightin,widthinr,heightinr;
    double      scale;
    double      lx,ly,dx,dy ;
    char        datej[20];
    char        editionj[5];
    int         result;
    const char  *sunits;
    FIBITMAP    *bitmap;
    FREE_IMAGE_TYPE type;
    RGBQUAD     palette[256*8];
    char        *filenameNU;
    double      londeg = 0;
    double      latdeg = 0;
    double      lat0loc = lat0;
    double      lat1loc = lat1;
    double      lon0loc = lon0;
    double      lon1loc = lon1;
    double      lon1locr, lon0locr, lat1locr, lat0locr;
    uint16_t    pixpos0xr,pixpos1xr,pixpos0yr,pixpos1yr;
    int         numxf = 0;
    int         xf[12];
    int         yf[12];
    int         i,ply = 0;
    double      plylat[12];
    double      plylon[12];

    FILE        *out;

    switch(units) {
    case METTERS: sunits = "METERS";  break;
    case FATHOMS: sunits = "FATHOMS"; break;
    case FEET:    sunits = "FEET";    break;
    default: fprintf(stderr, "ERROR - invalid units"); exit(1);
    }

    /* get latitude and longitude  */

    if (abs((int)lat0) > 85) return 1;
    if (abs((int)lon0) > 180) return 1;
    if (abs((int)lat1) > 85) return 1;
    if (abs((int)lon1) > 180) return 1;


    memset(palette,0,sizeof(palette));

    /* Read image file */
    bitmap = FreeImage_Load((FREE_IMAGE_FORMAT)typein,filein,BMP_DEFAULT);
    if (bitmap == NULL)
    {
        fprintf(stderr, "ERROR - Could not open or error in image file\"%s\"\n", filein);
        return 2;
    }

    type = FreeImage_GetImageType(bitmap);
    if (type != FIT_BITMAP)
    {
        fprintf(stderr, "ERROR - Image is not a bitmap file\"%s\"\n", filein);
        FreeImage_Unload(bitmap);
        return 2;
    }

    widthin = FreeImage_GetWidth(bitmap);
    heightin = FreeImage_GetHeight(bitmap);
    if (!widthin || !heightin)
    {
        fprintf(stderr, "ERROR - Invalid image size (width=%d,height=%d)\n", widthin, heightin);
        FreeImage_Unload(bitmap);
        return 2;
    }

    if (pixpos0x != -1 && pixpos0y != -1 && pixpos1x != -1 && pixpos1y != -1)
    {
        if (pixpos0x > widthin/3 && pixpos1x < widthin/3 && pixpos0y > heightin/3 && pixpos1y < heightin/3)
        {
            fprintf(stderr, "ERROR - x;y pixel position must be within the upper left third and the lower right third of the image\n");
            FreeImage_Unload(bitmap);
            return 2;
        }
        if (pixpos0x < 0 || pixpos1x >= widthin || pixpos0y < 0 || pixpos1y >= heightin)
        {
            fprintf(stderr, "ERROR - x;y pixel position is outside the image\n");
            FreeImage_Unload(bitmap);
            return 2;
        }

        // calculate degree/pixel and extend lon0,lat0 lon1,lat1 to the edges of the image
        widthoutr = widthinr = pixpos1x +1 -pixpos0x;
        heightoutr = heightinr = pixpos1y +1 -pixpos0y;

        // calculate the lon,lat of the edges of the image
        londeg = (lon1 - lon0) / widthinr;
        lon0loc = lon0 - pixpos0x * londeg;
        lon1loc = lon1 + (widthin -1 -pixpos1x) * londeg;
        latdeg = (lat0 - lat1) / heightinr;
        lat0loc = lat0 + pixpos0y * latdeg;
        lat1loc = lat1 - (heightin -1 -pixpos1y) * latdeg;

        pixpos0xr = pixpos0x; pixpos1xr = pixpos1x; pixpos0yr = pixpos0y; pixpos1yr = pixpos1y;
        if (optionwgs84 == 0)
        {
            lx = lontox(lon1)-lontox(lon0);
            if (lx < 0) lx = -lx;
            ly = lattoy_WS84(lat0)-lattoy_WS84(lat1);
            if (ly < 0) ly = -ly;

            // calculate extend widthout heightout relative
            dx = heightinr * lx / ly - widthinr;
            dy = widthinr * ly / lx - heightinr;

            if (dy < 0) widthoutr = (int)round(widthinr + dx) ;
            heightoutr = (int)round(widthoutr * ly / lx) ;

            // extend x,y of the given ref points with wgs84 correction
            pixpos0xr = (int)round (pixpos0x * widthoutr / widthinr);
            pixpos1xr = (int)round (pixpos1x * widthoutr / widthinr);
            pixpos0yr = (int)round (pixpos0y * heightoutr / heightinr);
            pixpos1yr = (int)round (pixpos1y * heightoutr / heightinr);
        }
    }

    // calculate REF positions if cut off an image frame
    if (optframe != NULL )
    {
        londeg = (lon1loc-lon0loc) / widthin;
        latdeg = (lat0loc-lat1loc) / heightin;
        numxf = sscanf (optframe, "%d;%d-%d;%d-%d;%d-%d;%d-%d;%d-%d;%d-%d;%d-%d;%d-%d;%d-%d;%d-%d;%d-%d;%d", &xf[0],&yf[0],&xf[1],&yf[1],&xf[2],&yf[2],&xf[3],&yf[3],&xf[4],&yf[4],&xf[5],&yf[5],&xf[6],&yf[6],&xf[7],&yf[7],&xf[8],&yf[8],&xf[9],&yf[9],&xf[10],&yf[10],&xf[11],&yf[11]);
        ply = 0;
        switch (numxf)
        {
        case 4 :
            ply = 3;
            plylon[0] = plylon[3] = lon0loc + (xf[0] * londeg);
            plylon[1] = plylon[2] = lon1loc - (widthin -1 -xf[1]) * londeg;
            plylat[0] = plylat[1] = lat0loc - (yf[0] * latdeg);
            plylat[2] = plylat[3] = lat1loc + (heightin -1 -yf[1]) * latdeg;
            break;
        case 24 :
            ply = 11;
            plylon[11] = lon0loc + (xf[11] * londeg);
            plylat[11] = lat0loc - (yf[11] * latdeg);
        case 22 :
            if (ply == 0) ply = 10;
            plylon[10] = lon0loc + (xf[10] * londeg);
            plylat[10] = lat0loc - (yf[10] * latdeg);
        case 20 :
        	if (ply == 0) ply = 9;
            plylon[9] = lon0loc + (xf[9] * londeg);
            plylat[9] = lat0loc - (yf[9] * latdeg);
        case 18 :
            if (ply == 0) ply = 8;
            plylon[8] = lon0loc + (xf[8] * londeg);
            plylat[8] = lat0loc - (yf[8] * latdeg);
        case 16 :
            if (ply == 0) ply = 7;
            plylon[7] = lon0loc + (xf[7] * londeg);
            plylat[7] = lat0loc - (yf[7] * latdeg);
        case 14 :
            if (ply == 0) ply = 6;
            plylon[6] = lon0loc + (xf[6] * londeg);
            plylat[6] = lat0loc - (yf[6] * latdeg);
        case 12 :
            if (ply == 0) ply = 5;
            plylon[5] = lon0loc + (xf[5] * londeg);
            plylat[5] = lat0loc - (yf[5] * latdeg);
        case 10 :
            if (ply == 0) ply = 4;
            plylon[4] = lon0loc + (xf[4] * londeg);
            plylat[4] = lat0loc - (yf[4] * latdeg);
        case 8 :
            if (ply == 0) ply = 3;
            plylon[3] = lon0loc + (xf[3] * londeg);
            plylat[3] = lat0loc - (yf[3] * latdeg);
        case 6 :
            if (ply == 0) ply = 2;
            plylon[0] = lon0loc + (xf[0] * londeg);
            plylat[0] = lat0loc - (yf[0] * latdeg);
            plylon[1] = lon0loc + (xf[1] * londeg);
            plylat[1] = lat0loc - (yf[1] * latdeg);
            plylon[2] = lon0loc + (xf[2] * londeg);
            plylat[2] = lat0loc - (yf[2] * latdeg);
            break;

        default :
            fprintf(stderr,"ERROR - use -r x0f;y0f-x1f;y1f to define an rectangle area in the image which is visible from the .kap\n");
            fprintf(stderr,"      - use -r x0f;y0f-x1f;y1f-x2f;y2f ... to define a up to 12 edges polygon which is visible from the .kap\n");
            FreeImage_Unload(bitmap);
            return 2;
        }
    }
    else
    {
        ply = 3;
        plylon[0] = plylon[3] = lon0loc;
        plylon[1] = plylon[2] = lon1loc;
        plylat[0] = plylat[1] = lat0loc;
        plylat[2] = plylat[3] = lat1loc;
    }

    out = fopen(fileout, "wb");
    if (! out)
    {
        fprintf(stderr,"ERROR - Can't open KAP file %s\n",fileout);
        FreeImage_Unload(bitmap);
        return 2;
    };

    /* Header comment file outut */

    if (edition != NULL && edition[0] == '\0')
    {
        strncpy(editionj, "1", 5);
    } else {
        strncpy(editionj, edition, 5);
    }

    /* Read date */
    if (updated != NULL && updated[0] == '\0')
    {
        time_t      t;
        struct tm   *date;

        time(&t) ;
        date = localtime(&t);
        strftime(datej, sizeof(datej), "%d/%m/%Y",date);
    } else {
        strncpy(datej, updated, 20);
    }

    /* Header comment file outut */
    fprintf(out,"! 2016 imgkap %s file generator by M'dJ, H.N\r\n", VERS);
    fprintf(out,"! Map generated not for navigation created at %s\r\n",datej);

    /* calculate size for WS84 */
    dpi = 254;
    heightout = heightin;
    widthout = widthin;

    lx = lontox(lon1loc)-lontox(lon0loc);
    if (lx < 0) lx = -lx;
    ly = lattoy_WS84(lat0loc)-lattoy_WS84(lat1loc);
    if (ly < 0) ly = -ly;

    if (optionwgs84 == 0)
    {
        /* calculate extend widthout heightout */
        dx = heightin * lx / ly - widthin;
        dy = widthin * ly / lx - heightin;

        if (dy < 0) widthout = (int)round(widthin + dx) ;
        heightout = (int)round(widthout * ly / lx) ;

        fprintf(out,"! Extend widthin %d heightin %d to widthout %d heightout %d\r\n",
                widthin,heightin,widthout,heightout);
    }

    scale = (1-(widthin/lx) / (heightin/ly)) *100;
    if ((scale > 5) || (scale < -5))
    {
    	fprintf(stderr,"ERROR - size of image is not correct\n");
        fprintf(stderr,"\t width = %dpx  LON-degree = %f \n\t height = %dpx  LAT-degree = %f\n",
            widthin, lon1loc-lon0loc, heightin, lat0loc-lat1loc );
        FreeImage_Unload(bitmap);
        fclose(out);
        return 2;
    }

    /* calculate resolution en size in meters */

    dx = postod((lat0loc+lat1loc)/2,lon0loc,(lat0loc+lat1loc)/2,lon1loc);
    dy = postod(lat0loc,lon0loc,lat1loc,lon0loc);
    fprintf(out,"! Size in milles %.2f x %.2f\r\n",dx,dy) ;

    float realscale = round(dy*18520000.0*dpi/(heightout*254));
    if (optionscale == 0)
    {
        scale = realscale;
    } else {
        scale = optionscale;
    }
    switch(units) {
    case METTERS:
        dx = dx*1852.0/(double)widthout;
        dy = dy*1852.0/(double)heightout;
        break;
    case FEET:
        dx *= 6;
        dy *= 6;
        // fallthrough
    case FATHOMS:
        dx = dx*1157500./((double)widthout*1143.);
        dy = dy*1157500./((double)heightout*1143.);
    }
    if (optionscale == 0)
    {
        fprintf(out,"! Resolution units %s - %.2fx%.2f -> %.0f at %d dpi\r\n", sunits, dx,dy,scale,dpi) ;
    } else {
        fprintf(out,"! Resolution units %s - %.2fx%.2f -> %.0f at %d dpi overriden to  %.0f\r\n", sunits, dx, dy, realscale, dpi, scale) ;
    }

    /* Write KAP header */
    if (color == COLOR_NONE)
        fputs("VER/2.0\r\n", out);
    else
        fputs("VER/3.0\r\n", out);

    fprintf(out,"CED/SE=%s,RE=01,ED=%s\r\n",edition,datej);

    /* write filename and date*/
    {
        char *s;

        s = fileout + strlen(fileout) -1;
        while ((s > fileout) && (*s != '.')) s--;
        if (s > fileout) *s = 0;
        while ((s > fileout) && (*s != '\\') && (*s != '/')) s--;
        if (s > fileout) s++;
        filenameNU = s;
        
        if (strlen (title) == 0)
        {
            title = s;
        }
         fprintf(out,"BSB/NA=%.70s\r\n",title);
    }

    fprintf(out,"    NU=%s,RA=%d,%d,DU=%d\r\n",filenameNU,widthout,heightout,dpi);
    fprintf(out,"KNP/SC=%0.f,GD=%s,PR=%s,PP=%.2f\r\n", scale, gd, pr,0.0);
    fputs("    PI=UNKNOWN,SP=UNKNOWN,SK=0.0,TA=90\r\n", out);
    fprintf(out,"    UN=%s,SD=%s,DX=%.2f,DY=%.2f\r\n", sunits, sd,dx,dy);

    fprintf(out,"REF/1,%u,%u,%f,%f\r\n",0,0,lat0loc,lon0loc);
    fprintf(out,"REF/2,%u,%u,%f,%f\r\n",widthout,0,lat0loc,lon1loc);
    fprintf(out,"REF/3,%u,%u,%f,%f\r\n",widthout,heightout,lat1loc,lon1loc);
    fprintf(out,"REF/4,%u,%u,%f,%f\r\n",0,heightout,lat1loc,lon0loc);

    if (pixpos0x != -1)
    {
        fprintf(out,"REF/5,%u,%u,%f,%f\r\n",pixpos0xr,pixpos0yr,lat0,lon0);
        fprintf(out,"REF/6,%u,%u,%f,%f\r\n",pixpos1xr,pixpos0yr,lat0,lon1);
        fprintf(out,"REF/7,%u,%u,%f,%f\r\n",pixpos1xr,pixpos1yr,lat1,lon1);
        fprintf(out,"REF/8,%u,%u,%f,%f\r\n",pixpos0xr,pixpos1yr,lat1,lon0);
    }

    for (i=0; i<=ply; i++)
    {
        fprintf(out,"PLY/%u,%f,%f\r\n",i+1,plylat[i],plylon[i]);
    }
    //ToDo
    //fprintf(out,"WPX/2...,...\r\n");
    //fprintf(out,"WPY/2...,...\r\n");
    //fprintf(out,"PWX/2...,...\r\n");
    //fprintf(out,"PWY/2...,...\r\n");

    fprintf(out,"DTM/%.6f,%.6f\r\n", 0.0, 0.0);

    result = writeimgkap(out,&bitmap,optkap,color,(Color32 *)palette,widthin,heightin,widthout,heightout);
    FreeImage_Unload(bitmap);
    fclose(out);

    return result;
}

typedef struct sxml
{
    struct sxml *child;
    struct sxml *next;
    char tag[1];
} mxml;

static int mxmlreadtag(FILE *in, char *buftag)
{
    int c;
    int end = 0;
    int endtag = 0;
    int i = 0;

    while ((c = getc(in)) != EOF)
        if (strchr(" \t\r\n",c) == NULL) break;

    if (c == EOF) return -1;

    if (c == '<')
    {
        endtag = 1;
        c = getc(in);
        if (strchr("?!/",c) != NULL)
        {
            end = 1;
            c = getc(in);
        }
    }

    if (c == EOF) return -1;

    do
    {
        if (endtag && (c == '>')) break;
        else if (c == '<')
        {
            ungetc(c,in);
            break;
        }
        buftag[i++] = c;

        if (i > 1024)
            return -1;

    } while ((c = getc(in)) != EOF) ;

    while ((i > 0) && (strchr(" \t\r\n",buftag[i-1]) != NULL)) i--;

    buftag[i] = 0;

    if (end) return 1;
    if (endtag) return 2;
    return 0;
}

static int mistag(char *tag, const char *s)
{
    while (*s)
    {
        if (!*tag || (*s != *tag)) return 0;
        s++;
        tag++;
    }
    if (!*tag || (strchr(" \t\r\n",*tag) != NULL)) return 1;
    return 0;
}

static mxml *mxmlread(FILE *in, mxml *parent, char *buftag)
{
    int r;
    mxml *x , *cur , *first;

    x = cur = first = 0 ;

    while ((r = mxmlreadtag(in,buftag)) >= 0)
    {
        if (parent && mistag(parent->tag,buftag)) return first;

        x = (mxml *)myalloc(sizeof(mxml)+strlen(buftag)+1);
        if (x == NULL)
        {
            fprintf(stderr,"ERROR - Intern malloc\n");
            return first;
        }
        if (!first) first = x;
        if (cur) cur->next = x;
        cur = x;

        x->child = 0;
        x->next = 0;
        strcpy(x->tag,buftag);

        if (!r) break;
        if (r > 1) x->child = mxmlread(in,x,buftag);
    }
    return first;
}


static mxml *mxmlfindtag(mxml *first,const char *tag)
{
    while (first)
    {
        if (mistag(first->tag,tag)) break;
        first = first->next;
    }
    return first;
}

#define mxmlfree(x) myfree()

static int readkml(char *filein,double *lat0, double *lon0, double *lat1, double *lon1, char *title)
{
    int         result;
    mxml        *kml,*ground,*cur;
    FILE        *in;
    char        *s;
    char        buftag[1024];

    in = fopen(filein, "rb");
    if (in == NULL)
    {
        fprintf(stderr,"ERROR - Can't open KML file %s\n",filein);
        return 2;
    }

    if (*filein)
    {
        s = filein + strlen(filein) - 1;
        while ((s >= filein) && (strchr("/\\",*s) == NULL)) s--;
         s[1] = 0;
    }

    kml = mxmlread(in,0,buftag);
    fclose(in);

    if (kml == NULL)
    {
        fprintf(stderr,"ERROR - Not XML KML file %s\n",filein);
        return 2;
    }

    ground = mxmlfindtag(kml,"kml");

    result = 2;
    while (ground)
    {
        ground = mxmlfindtag(ground->child,"GroundOverlay");
        if (!ground || ground->next)
        {
            fprintf(stderr,"ERROR - KML no GroundOverlay or more one\n");
            break;
        }
        cur = mxmlfindtag(ground->child,"name");
        if (!cur || !cur->child)
        {
            fprintf(stderr,"ERROR - KML no Name\n");
            break;
        }
        if (!*title)
            strcpy(title,cur->child->tag);

        cur = mxmlfindtag(ground->child,"Icon");
        if (!cur || !cur->child)
        {
            fprintf(stderr,"ERROR - KML no Icon\n");
            break;
        }
        cur = mxmlfindtag(cur->child,"href");
        if (!cur || !cur->child)
        {
            fprintf(stderr,"ERROR - KML no href\n");
            break;
        }
        strcat(filein,cur->child->tag);

#if (defined(_WIN32) || defined(__WIN32__))
        s = filein + strlen(filein);
        while (*s)
        {
            if (*s == '/') *s = '\\';
            s++;
        }
#endif

        cur = mxmlfindtag(ground->child,"LatLonBox");
        if (!cur || !cur->child)
        {
            fprintf(stderr,"ERROR - KML no LatLonBox\n");
            break;
        }
        result = 3;
        ground = cur->child;

        cur = mxmlfindtag(ground,"rotation");
        if (cur && cur->child)
        {
            *lat0 = strtod(cur->child->tag,&s);
            if (*s || (*lat0 > 0.5))
            {
                result = 4;
                fprintf(stderr,"ERROR - KML rotation is not accepted\n");
                break;
            }
        }
        cur = mxmlfindtag(ground,"north");
        if (!cur || !cur->child) break;
        *lat0 = strtod(cur->child->tag,&s);
        if (*s) break;

        cur = mxmlfindtag(ground,"south");
        if (!cur || !cur->child) break;
        *lat1 = strtod(cur->child->tag,&s);
        if (*s) break;

        cur = mxmlfindtag(ground,"west");
        if (!cur || !cur->child) break;
        *lon0 = strtod(cur->child->tag,&s);
        if (*s) break;

        cur = mxmlfindtag(ground,"east");
        if (!cur || !cur->child) break;
        *lon1 = strtod(cur->child->tag,&s);
        if (*s) break;

        result = 0;
        break;
    }

    mxmlfree(kml);

    if (result == 3) fprintf(stderr,"ERROR - KML no Lat Lon\n");
    return result;
}

#ifndef LIBIMGKAP

double mystrtod(char *s, char **end)
{
    double d = 0, r = 1;

    while ((*s >= '0') && (*s <= '9')) {d = d*10 + (*s-'0'); s++;}
    if ((*s == '.') || (*s == ','))
    {
        s++;
        while ((*s >= '0') && (*s <= '9')) { r /= 10; d += r * (*s-'0'); s++;}
    }
    *end = s;
    return d;
}


double strtopos(char *s, char **end)
{
    int     sign = 1;
    double  degree = 0;
    double  minute = 0;
    double  second = 0;

    char c;

    *end = s;

   /* eliminate space */
    while (*s == ' ') s++;

    /* begin read sign */
    c = toupper(*s);
    if ((c == '-') || (c == 'O') || (c == 'W') || (c == 'S')) {s++;sign = -1;}
    if ((c == '+') || (c == 'E') || (c == 'N')) s++;

    /* eliminate space */
    while (*s == ' ') s++;

    /* error */
    if (!*s) return 0;

    /* read degree */
    degree = mystrtod(s,&s);

    /* eliminate space and degree */
    while ((*s == ' ') || (*s == 'd') || (*s < 0)) s++;

    /* read minute */
    minute = mystrtod(s,&s);

    /* eliminate space and minute separator */
    while ((*s == ' ') || (*s == 'm') || (*s == 'n') || (*s == '\'')) s++;

    /* read second */
    second = mystrtod(s,&s);

    /* eliminate space and second separator*/
    while ((*s == ' ') || (*s == '\'') || (*s == '\"') || (*s == 's')) s++;

    /* end read sign */
    c = toupper(*s);
    if ((c == '-') || (c == 'O') || (c == 'W') || (c == 'S')) {s++;sign = -1;}
    if ((c == '+') || (c == 'E') || (c == 'N')) s++;

   /* eliminate space */
    while (*s == ' ') s++;

    *end = s;
    return sign * (degree + ((minute + (second/60.))/60.));
}

static void makefileout(char *fileout, const char *filein)

{
    char *s;

    strcpy(fileout,filein);

    s = fileout + strlen(fileout)-1;
    while ((s > fileout) && (*s != '.')) s--;

    if (s > fileout) strcpy(s,".kap");
}


/* Main programme */

int main (int argc, char *argv[])
{
    int     result = 0;
    char    filein[1024];
    char    *fileheader = NULL;
     char    fileout[1024];
     int     typein, typeheader,typeout;
    char    *optionsd ;
    char    *optionframe;
    int     optionunits = METTERS;
    int     optionkap = NORMAL;
    int     optionrc = 0;
    int     optionwgs84 = 0;
    int     optcolor;
    char    *optionpal ;
    char    optiontitle[256];
    char    optionupdated[256];
    char    optionedition[256];
    double  lat0,lon0,lat1,lon1;
    double  l;
    int        pixpos0x = -1;
    int        pixpos0y = -1;
    int        pixpos1x = -1;
    int        pixpos1y = -1;
    int        pixposx = 0;
    int        pixposy = 0;
    char       optiongd[256];
    char       optionpr[256];
    int        optionscale = 0;

    optionsd = (char *)"UNKNOWN" ;
    optionpal = NULL;
    optionframe = NULL;
    strcpy(optiongd, "WGS84");
    strcpy(optionpr, "MERCATOR");

    typein = typeheader = typeout = FIF_UNKNOWN;
    lat0 = lat1 = lon0 = lon1 = HUGE_VAL;
    *filein = *fileout = *optiontitle = *optionupdated = *optionedition = 0;

    while (--argc)
    {
        argv++;
        if (*argv == NULL) break;
        if (((*argv)[0] == '-') && ((*argv)[2] == 0))
        {
            /* options */
            char c = toupper((*argv)[1]);
            if (c == 'N')
            {
                optionkap = OLDKAP;
                continue;
            }
            if (c == 'C')
            {
            	optionrc = 1;
				continue;
            }
            if (c == 'F')
            {
                optionunits = FATHOMS;
                continue;
            }
            if (c == 'E')
            {
                optionunits = FEET;
                continue;
            }
            if (c == 'W')
            {
                optionwgs84 = 1;
                continue;
            }
            if (c == 'R')
            {
                if (argc > 1) optionframe = argv[1];
                argc--;
                argv++;
                continue;
            }
            if (c == 'S')
            {
                if (argc > 1) optionsd = argv[1];
                argc--;
                argv++;
                continue;
            }
            if (c == 'T')
            {
                if (argc > 1) strcpy(optiontitle,argv[1]);
                argc--;
                argv++;
                continue;
            }
            if (c == 'I')
            {
                if (argc > 1) strcpy(optionedition,argv[1]);
                argc--;
                argv++;
                continue;
            }
            if (c == 'U')
            {
                if (argc > 1) strcpy(optionupdated,argv[1]);
                argc--;
                argv++;
                continue;
            }
            if (c == 'P')
            {
                if (argc > 1) optionpal = argv[1];
                argc--;
                argv++;
                continue;
            }
            if (c == 'D')
            {
                if (argc > 1) strcpy(optiongd,argv[1]);
                argc--;
                argv++;
                continue;
            }
            if (c == 'J')
            {
                if (argc > 1) strcpy(optionpr,argv[1]);
                argc--;
                argv++;
                continue;
            }
            if (c == 'L')
            {
                if (argc > 1) optionscale = strtol(argv[1], NULL, 0);
                argc--;
                argv++;
                continue;
            }
            if ((c < '0') || (c > '9'))
            {
                result = 1;
                break;
            }
        }
        if (!*filein)
        {
            strcpy(filein,*argv);
            continue;
        }
        if (fileheader == NULL)
        {
            /* if numeric */
            // 2 pixel positions of lat,lon from command line
            if ( sscanf(*argv, "%d;%d" , &pixposx, &pixposy) == 2 )
            {
                if (pixpos0x < 0)
                {
                    pixpos0x = pixposx;
                    pixpos0y = pixposy;
                    continue;
                }
                if (pixpos1x < 0)
                {
                    pixpos1x = pixposx;
                    pixpos1y = pixposy;
                    // if x0 > x1 mirror positions
                    if (pixpos0x > pixpos1x)
                    {
                    	pixpos1x = pixpos0x;
                    	pixpos0x = pixposx;
                    }
                    continue;
                }
                result = 1;
                break;
            }
            else
            {
                char *s;
                // 2 lat0,lon0 lat1,lon1 positions
                l = strtopos(*argv,&s);
                if (!*s)
                {
                    if (lat0 == HUGE_VAL)
                    {
                        lat0 = l;
                        continue;
                    }
                    if (lon0 == HUGE_VAL)
                    {
                        lon0 = l;
                        continue;
                    }
                    if (lat1 == HUGE_VAL)
                    {
                        lat1 = l;
                        continue;
                    }
                    if (lon1 == HUGE_VAL)
                    {
                   		lon1 = l;
                        // if lon0 > lon1 mirror positions
                    	if (lon0 > l)
                        {
                        	lon1 = lon0;
                        	lon0 = l;
                        }
                    	continue;
                    }
                    result = 1;
                    break;
                }
                fileheader = *argv;
                continue;
            }
        }
        if (!*fileout)
        {
            strcpy(fileout,*argv);
            continue;
        }
        result = 1;
        break;
    }
    if (!*filein) result = 1;

    if (!result)
    {
        FreeImage_Initialise(0);

        typein = findfiletype(filein);
        if (typein == FIF_UNKNOWN) typein = (int)FreeImage_GetFileType(filein,0);

        switch (typein)
        {
            case FIF_KML :
                if (fileheader != NULL) strcpy(fileout,fileheader);

                result = readkml(filein,&lat0,&lon0,&lat1,&lon1,optiontitle);
                if (result) break;

                if (!*fileout) makefileout(fileout,filein);

                typein = (int)FreeImage_GetFileType(filein,0);
                optcolor = COLOR_NONE;
                if (optionpal) optcolor = findoptlist(listoptcolor,optionpal);
                result = imgtokap(typein,filein,lat0,lon0,pixpos0x,pixpos0y,lat1,lon1,pixpos1x,pixpos1y,optionkap,optcolor,optiontitle,optionunits,optionsd,optionwgs84,optionframe,fileout,optiongd,optionpr,optionscale,optionedition,optionupdated);
                break;

            case FIF_KAP :
            case FIF_NO1 :
                if (fileheader == NULL)
                {
                    result = 1;
                    break;
                }
                typeheader = findfiletype(fileheader);
                if (typeheader == FIF_UNKNOWN)
                {
                    if (*fileout)
                    {
                        result = 1;
                        break;
                    }
                    typeout = typeheader;
                    typeheader = FIF_UNKNOWN;
                    strcpy(fileout,fileheader);
                    fileheader = NULL;
                }
                if (*fileout)
                {
                    typeout = FreeImage_GetFIFFromFilename(fileout);
                    if (typeout == FIF_UNKNOWN)
                    {
                        result = 1;
                        break;
                    }
                }
                if (!*fileout && (typeheader == FIF_KAP))
                {
                    optcolor = COLOR_KAP;
                    if (optionpal) optcolor = findoptlist(listoptcolor,optionpal);

                    result = imgheadertokap(typein,filein,typein,optionkap,optionrc,optcolor,optiontitle,filein,fileheader);
                    break;
                }
                result = kaptoimg(typein,filein,typeheader,fileheader,typeout,fileout,optionpal);
                break;

            case (int)FIF_UNKNOWN:
                fprintf(stderr, "ERROR - Could not open or error in image file\"%s\"\n", filein);
                result = 2;
                break;
            default:
                    if ((lon1 != HUGE_VAL) && (fileheader != NULL))
                    {
                        strcpy(fileout,fileheader);
                        fileheader = NULL;
                    }

                    optcolor = COLOR_NONE;
                    if (optionpal) optcolor = findoptlist(listoptcolor,optionpal);

                    if (!*fileout) makefileout(fileout,filein);
                    if (fileheader != NULL)
                    {
                        typeheader = findfiletype(fileheader);
                        result = imgheadertokap(typein,filein,typeheader,optionkap,optionrc,optcolor,optiontitle,fileheader,fileout);
                        break;
                    }
                    if (lon1 == HUGE_VAL)
                    {
                        result = 1;
                        break;
                    }
                    result = imgtokap(typein,filein,lat0,lon0,pixpos0x,pixpos0y,lat1,lon1,pixpos1x,pixpos1y,optionkap,optcolor,optiontitle,optionunits,optionsd,optionwgs84,optionframe,fileout,optiongd,optionpr,optionscale,optionedition,optionupdated);
                break;
        }
        FreeImage_DeInitialise();
    }

    // si kap et fileheader avec ou sans  file out lire kap - > image ou header et image
    // sinon lire image et header ou image et position -> kap

    if (result == 1)
    {
        fprintf(stderr, "ERROR - Usage: imgkap [option] [inputfile] [lat0 lon0 [x0;y0] lat1 lon1 [x1;y1] | headerfile] [outputfile]\n");
        fprintf(stderr, "\nUsage of imgkap Version %s by M'dJ + H.N\n",VERS);
        fprintf(stderr, "\nConvert kap to img :\n");
        fprintf(stderr,  "  >imgkap mykap.kap myimg.png\n" );
        fprintf(stderr,  "    -convert mykap into myimg.png\n");
        fprintf(stderr,  "  >imgkap mykap.kap mheader.kap myimg.png\n" );
        fprintf(stderr,  "    -convert mykap into header myheader (only text file) and myimg.png\n" );
        fprintf(stderr, "\nConvert img to kap : \n");
        fprintf(stderr,  "  >imgkap myimg.png myheaderkap.kap\n" );
        fprintf(stderr,  "    -convert myimg.png into myresult.kap using myheader.kap for kap informations\n" );
        fprintf(stderr,  "  >imgkap mykap.png lat0 lon0 lat1 lon1 myresult.kap\n" );
        fprintf(stderr,  "    -convert myimg.png into myresult.kap using WGS84 positioning\n" );
        fprintf(stderr,  "  >imgkap mykap.png lat0 lon0 x0;y0 lat1 lon1 x1;y1 myresult.kap\n" );
        fprintf(stderr,  "    -convert myimg.png into myresult.kap\n" );
        fprintf(stderr,  "  >imgkap -s 'LOWEST LOW WATER' myimg.png lat0 lon0 lat1 lon2 -f\n" );
        fprintf(stderr,  "    -convert myimg.png into myimg.kap using WGS84 positioning and options\n" );
        fprintf(stderr, "\nConvert kml to kap : \n");
        fprintf(stderr,  "  >imgkap mykml.kml\n" );
        fprintf(stderr,  "    -convert GroundOverlay mykml file into kap file using name and dir of image\n" );
        fprintf(stderr,  "  >imgkap mykml.kml mykap.kap\n" );
        fprintf(stderr,  "    -convert GroundOverlay mykml into mykap file\n" );
        fprintf(stderr, "\nWGS84 positioning :\n");
        fprintf(stderr, "\tlat0 lon0 is a left or right,top point\n");
        fprintf(stderr, "\tlat1 lon1 is a right or left,bottom point cater-cornered to lat0 lon0\n");
        fprintf(stderr, "\tlat to be between -85 and +85 degree\n");
        fprintf(stderr, "\tlon to be between -180 and +180 degree\n");
        fprintf(stderr, "\t    different formats are accepted : -1.22  1°10'20.123N  -1d22.123 ...\n");
        fprintf(stderr, "\tx;y pixel points can be used if lat lon defines not the image edges.\n");
        fprintf(stderr, "\t    lat0 lon0 x0;y0 must be in the left or right, upper third\n");
        fprintf(stderr, "\t    lat1 lon1 x1;y1 must be in the right or left, lower third\n");
        fprintf(stderr, "Options :\n");
        fprintf(stderr,  "\t-w  : no image size extension to WGS84 because image is already WGS84\n" );
        fprintf(stderr,  "\t-r x0f;y0f-x1f;y1f  \"2 pixel points -> 4 * PLY\"\n");
        fprintf(stderr,  "\t    : define a rectangle area in the image visible from the .kap\n" );
        fprintf(stderr,  "\t-r x0f;y0f-x1f;y1f-x2f;y2f-x3f;y3f... \"3 to 12 pixel points -> PLY\"\n");
        fprintf(stderr,  "\t    : define a up to 12 edges polygon visible from the .kap\n" );
        fprintf(stderr,  "\t-n  : Force compatibility all KAP software, max 127 colors\n" );
        fprintf(stderr,  "\t-c  : reduce colors of image to 127 colors / for test purposes only\n" );
        fprintf(stderr,  "\t-f  : fix units to FATHOMS\n" );
        fprintf(stderr,  "\t-e  : fix units to FEET\n" );        
        fprintf(stderr,  "\t-s name : fix sounding datum\n" );
        fprintf(stderr,  "\t-t title : change name of map\n" );
        fprintf(stderr,  "\t-j projection : change projection of map (Default: MERCATOR)\n" );
        fprintf(stderr,  "\t-d datum : change geographic datum of map (Default: WGS84)\n" );
        fprintf(stderr,  "\t-l scale : Override the calculated scale, 1:<SCALE> (Default: automatically calculated from the image size and geographic extent)\n" );
        fprintf(stderr,  "\t-i edition : Source edition of the chart (Default: 1)\n" );
        fprintf(stderr,  "\t-u updated : Date of the last update (Default: current date)\n" );
        fprintf(stderr,  "\t-p color : color of map\n" );
        fprintf(stderr,  "\t   color (Kap to image) : ALL|RGB|DAY|DSK|NGT|NGR|GRY|PRC|PRG\n" );
        fprintf(stderr,  "\t     ALL generate multipage image, use only with GIF or TIF\n" );
        fprintf(stderr,  "\t   color (image or Kap to Kap) :  NONE|KAP|MAP|IMG\n" );
        fprintf(stderr,  "\t     NONE use colors in image file, default\n" );
        fprintf(stderr,  "\t     KAP only width KAP or header file, use RGB tag in KAP file\n" );
        fprintf(stderr,  "\t     MAP generate DSK and NGB colors for map scan\n");
        fprintf(stderr,  "\t       < 64 colors: Black -> Gray, White -> Black\n" );
        fprintf(stderr,  "\t     IMG generate DSK and NGB colors for image (photo, satellite...)\n" );

        return 1;
    }
    if (result) fprintf(stderr,  "ERROR - imgkap (%s) return %d\n", argv[1], result );
    return result;
}

#endif
