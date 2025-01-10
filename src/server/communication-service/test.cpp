
#include "CapioCommunicationService.h"
//ADD TEST CPP TO CMAKE PROJECT
int main(int argc, char** argv){
    std::string port = argv[1];
    CapioCommunicationService CapioCommunicationService(port); // hostanmes non serve
    uint64_t NumberBytes = 50;

    //parte del codice di chi vuole ricevere
    /*

           char buff[NumberBytes];
           std::string  HostnameFrom = tcp.recive( buff, NumberBytes);
           std::cout << "il server ha ricevuto da "<< HostnameFrom << ": "<< buff << "\n" ;
    */


    //parte del codice di chi vuole mandare
    char buff[5]{'p','i','n','g','\0'};
    std::string ReceiverHostname = "fd-01"; //conosco l'hostname della persona a cui volgio mandare
    CapioCommunicationService.send(ReceiverHostname, buff, NumberBytes);

    return 0;
}


