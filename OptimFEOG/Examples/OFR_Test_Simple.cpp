/* Copyright (C) 1996-2015 Florin Ghido, all rights reserved. */
/* This file is part of OptimFROG SDK, see OptimFROG.h for details. */


// OFR_Test_Simple.cpp contains an example using the simple C++ interface.


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "OptimFROG/OptimFROGDecoder.h"


sInt32_t fatalError(const char* message)
{
    fprintf(stderr, "\nFatal error: %s!\n\n", message);
    return 1;
}

void myCallback(void* callBackParam, Float64_t percentage)
{
    (void) callBackParam;
    fprintf(stderr, "\015Decompressing %3d%%", (sInt32_t) percentage);
}

sInt32_t decodeFile(char* sourceFile, char* destinationFile)
{
    printf("\n");
    printf("sourceFile:      %s\n", sourceFile);
    printf("destinationFile: %s\n", destinationFile);
    printf("\n");

    sInt32_t result = OptimFROGDecoder::decodeFile(sourceFile, destinationFile, myCallback);

    if (result == OptimFROG_MemoryError)
        return fatalError("could not create instance");
    if (result == OptimFROG_OpenError)
        return fatalError("could not open source OFR/OFS file");
    if (result == OptimFROG_WriteError)
        return fatalError("could not open destination file");
    if (result == OptimFROG_FatalError)
        return fatalError("could not read source file");

    fprintf(stderr, "\015Decompressing done.\n");

    if (result == OptimFROG_RecoverableError)
        fprintf(stderr, "recoverable errors occured decompressing %s\n", sourceFile);
    printf("\n");

    if (result == OptimFROG_RecoverableError)
        return 1;
    else
        return 0;
}

sInt32_t infoFile(char* sourceFile)
{
    OptimFROGDecoder ofr_dec;

    sInt32_t result = ofr_dec.getFileDetails(sourceFile, C_TRUE);

    if (result == OptimFROG_MemoryError)
        return fatalError("could not create instance");
    if (result == OptimFROG_OpenError)
        return fatalError("could not open source OFR/OFS file");

    printf("\n");
    printf("fileName: %s\n", sourceFile);
    printf("\n");
    printf("channels:                    %2u\n", ofr_dec.channels);
    printf("bitspersample:               %2u\n", ofr_dec.bitspersample);
    printf("samplerate:              %6u\n", ofr_dec.samplerate);
    printf("length:              %10.3lf\n", ofr_dec.length_ms / 1000.0);
    printf("compressedSize:      %10.0lf\n", (Float64_t) ofr_dec.compressedSize);
    printf("originalSize:        %10.0lf\n", (Float64_t) ofr_dec.originalSize);
    printf("bitrate:                   %4u\n", ofr_dec.bitrate);
    printf("version:                   %4u\n", ofr_dec.version);
    printf("method:                  %s\n", ofr_dec.method);
    printf("speedup:                     %s\n", ofr_dec.speedup);
    printf("sampleType:              %s\n", ofr_dec.sampleType);
    printf("channelConfig:        %s\n", ofr_dec.channelConfig);
    printf("\n");

    for (uInt32_t i = 0; i < ofr_dec.keyCount; i++)
    {
        printf("%s: %s\n", ofr_dec.keys[i], ofr_dec.values[i]);
    }
    printf("\n");

    return 0;
}

void replaceExtension(char* name, const char* extension)
{
    sInt32_t pos = (sInt32_t) strlen(name) - 1;
    while (pos >= 0)
    {
        if (name[pos] == '.')
        {
            name[pos] = 0;
            break;
        }
        else if ((name[pos] == '\\') || (name[pos] == '/'))
        {
            break;
        }
        --pos;
    }

    strcat(name, extension);
}

int main(int argc, char* argv[])
{
    uInt32_t version = OptimFROG_getVersion();

    fprintf(stderr, "\n");
    fprintf(stderr, "Simple C++ Decoder for OptimFROG Lossless/DualStream Audio Codec v%d.%03d\n",
        version / 1000, version % 1000);
    fprintf(stderr, "Built on %s %s using configuration %s.\n", __DATE__, __TIME__, CFG_BUILD_NAME);
    fprintf(stderr, "Copyright (C) 1996-2015 Florin Ghido, all rights reserved.\n");
    fprintf(stderr, "Visit http://LosslessAudio.org/ for updates and more information.\n");
    fprintf(stderr, "@OptimFROG is also on Twitter. E-mail: florin.ghido@gmail.com\n");
    fprintf(stderr, "SDK and Libraries are free for non-commercial and commercial use.\n");
    fprintf(stderr, "\n");

    if (argc < 3)
    {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "%s {d|i} sourceFile [destinationFile]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "    d            decode an OFR/OFS file to a WAV/RAW file\n");
        fprintf(stderr, "    i            print information about an OFR/OFS file\n");
        return 2;
    }

    if (strcmp(argv[1], "d") == 0)
    {
        char* destinationFile;

        char temp[300];
        if (argc == 3)
        {
            strcpy(temp, argv[2]);
            replaceExtension(temp, ".wav");
            destinationFile = temp;
        }
        else
        {
            destinationFile = argv[3];
        }

        return decodeFile(argv[2], destinationFile);
    }
    else if (strcmp(argv[1], "i") == 0)
    {
        return infoFile(argv[2]);
    }

    return 2;
}
