#ifndef HEADER_FILE
#define HEADER_FILE

#define PATH_MAX 50
#define ARGS_MAX 10

typedef struct requestPackets{
    //normal request information
    char file[PATH_MAX];
    int nParams;
    char params[ARGS_MAX][PATH_MAX];

    //optional arguments
    bool sigtermFlag;
    int timeout;


    bool logFlag;
    char logFile[PATH_MAX];

    bool outFlag;
    char outFile[PATH_MAX];

    //information for mem request
    bool memFlag;
    int memRequestPID; //optional

    //information for memkill request
    bool memKillFlag;
    float memKillPercent;

}requestPacket;

#endif