#include "SigprocFile.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

std::string gInputFilFiles="in1.fil,in2.fil,in3.fil";
std::string gAvgSpectrumFile="avg_spectrum.txt";
std::vector<std::string> input_fil_files;
std::string gOutputFilFile="out.fil";
int gFOFFSign=1;
bool gOverSampled=false;
bool gFreddaCompatibleOutput=false;

void usage()
{
   printf("merge_coarse_channels in1.fil,in2.fil,in3.fil output_filfile\n");   
   printf("-s FOFF_SIGN : sign of FOFF (+1 or -1 to flip it) [default %d], 1-no change, -1 flip sign\n",gFOFFSign);
   printf("-o : files are oversampled in the same SKA-Low coarse channels are (32/27) [default %d]\n",gOverSampled);
   printf("-F : make sure FREDDA will be happy, for example : number of channels divides by 128 etc\n");
}

void parse_cmdline(int argc, char * argv[]) {
   char optstring[] = "hs:oF";
   int opt;
   opterr = 0;

   while ((opt = getopt(argc, argv, optstring)) != -1) {
      switch (opt) {
         case 's':
            if( optarg ){   
               gFOFFSign = atol( optarg );
            }
            break;

         case 'F':
            gFreddaCompatibleOutput = true;
            break;

         case 'o':
            gOverSampled=true;
            break;

         case 'h':
            usage();
            exit(EXIT_SUCCESS);

         default:
            fprintf(stderr,"Unknown or incomplete option -%c\n",optopt);
            usage();
            exit(EXIT_FAILURE);
      }
   }

   // strtok_r modifies its input, so tokenize a writable copy of the file list.
   std::vector<char> input_copy(gInputFilFiles.begin(), gInputFilFiles.end());
   input_copy.push_back('\0');
   char* saveptr = NULL;
   for( char* token = strtok_r(input_copy.data(), ",", &saveptr);
        token != NULL; token = strtok_r(NULL, ",", &saveptr) ){
      std::string value(token);
      size_t first = value.find_first_not_of(' ');
      size_t last = value.find_last_not_of(' ');
      input_fil_files.push_back(first == std::string::npos ? "" : value.substr(first, last-first+1));
   }
}

void print_parameters()
{
  printf("#####################################\n");
  printf("PARAMETERS :\n");
  printf("#####################################\n");
  printf("Input  fil files   = %s\n",gInputFilFiles.c_str());
  for(const auto& input : input_fil_files){
     printf("\t%s\n",input.c_str());
  }
  printf("Output fil file  = %s\n",gOutputFilFile.c_str());
  printf("Average spectrum output file = %s\n",gAvgSpectrumFile.c_str());
  printf("FOFF SIGN = %d\n",gFOFFSign);
  printf("Channels oversampled = %d\n",gOverSampled);
  printf("FREDDA compatible    = %d\n",gFreddaCompatibleOutput);
  printf("#####################################\n");
  fflush(stdout);
}

int main(int argc,char* argv[])
{
   if( argc<2 || (argc==2 && strcmp(argv[1],"-h")==0) ){
      usage();
      exit(0);
   }


   if( argc >= 2 ){
      gInputFilFiles = argv[1];
   }
   if( argc >= 3 ){
      gOutputFilFile = argv[2];
   }
   // Use the output prefix before the final dot, or an empty prefix if absent.
   size_t dot = gOutputFilFile.find_last_of('.');
   gAvgSpectrumFile = (dot == std::string::npos ? "" : gOutputFilFile.substr(0, dot)) + ".avg_spectrum";
   
   // getopt treats the output filename as argv[0] and parses the following options.
   parse_cmdline(argc-2,argv+2);
   print_parameters();

   // The selected merge allocates the per-channel averages for the caller.
   double* avg_spectrum = NULL;  
   int out_channels = 0;
   if( gOverSampled ){
      out_channels = SigprocFile::MergeOversampledCoarseChannels( input_fil_files , gOutputFilFile.c_str(), avg_spectrum, gFOFFSign, gFreddaCompatibleOutput );
   }else{
      out_channels = SigprocFile::MergeCoarseChannels( input_fil_files , gOutputFilFile.c_str(), avg_spectrum, gFOFFSign );   
   }

   if( out_channels < 0 ){
      delete [] avg_spectrum;
      return EXIT_FAILURE;
   }

   FILE* out_f = fopen( gAvgSpectrumFile.c_str(), "w" );
   if( !out_f ){
      perror( gAvgSpectrumFile.c_str() );
      delete [] avg_spectrum;
      return EXIT_FAILURE;
   }
   // Store one channel index and its mean value per line.
   for(int ch=0;ch<out_channels;ch++){
      fprintf(out_f,"%d %.4f\n",ch,avg_spectrum[ch]);
   }
   fclose(out_f);   
   
   if( avg_spectrum ){
      delete [] avg_spectrum;
   }      
}
