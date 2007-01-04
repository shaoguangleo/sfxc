#ifndef RUN_PRMS_H
#define RUN_PRMS_H
/**
CVS keywords
$Author$
$Date$
$Name$
$Revision$
$Source$

Class definitions for Run Parameters

Author     : RHJ Oerlemans
StartDate  : 20060912
Last change: 20061114

**/


class RunP
{
  public:

    //parameters which control how the program is run
    int  messagelvl;      //message level
    int  interactive;     //run interactive or automatically
    int  runoption;       //run option, default 1=complete
    
        
  public:

    //default constructor, set default values 
    RunP();

    //parse control file for run parameters
    int parse_ctrlFile(char *ctrlFile);

    //check run parameters
    int check_params();

    //get functions
    int get_messagelvl();
    int get_interactive();
    int get_runoption();
};


#endif // RUN_PRMS_H
