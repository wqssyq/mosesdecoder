#include <iostream>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include "Main.h"
#include "Simple.h"
#include "Compound.h"

using namespace std;

int main(int argc, char **argv)
{
	cerr << "starting" << endl;

  int method = 1; // 1=simple, 2=compound
  bool outWords = false;
  
  namespace po = boost::program_options;
  po::positional_options_description m_positional;
  po::options_description desc("Options");
  po::variables_map m_variables;
  
  desc.add_options()
  ("help", "Print help messages")
  ("method", po::value<int>()->default_value(method), "Method. 1=Simple(default), 2=Compound")
  ("input", po::value<vector<string> >()->composing(), "")
  ;
  
  vector<string> additionalParameters;
  
  po::variables_map vm;
  try {
    po::parsed_options parsed = po::command_line_parser(argc, argv).
    options(desc).allow_unregistered().run();
    
    po::store(parsed, vm); // can throw
  
    additionalParameters = collect_unrecognized(parsed.options, 
      po::include_positional);
    
    /** --help option
     */
    if ( vm.count("help") || argc < 3 ) {
      std::cout << argv[0] << " Unsplit Split [options...]" << std::endl
                << desc << std::endl;
      return EXIT_SUCCESS;
    }

    po::notify(vm); // throws on error, so do after help in case
    // there are any problems
  } catch(po::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
    std::cerr << desc << std::endl;
    return EXIT_FAILURE;
  }

  if (vm.count("method")) method = vm["method"].as<int>();
  
  
  // BEGIN
	string corpusPath, testPath;
	corpusPath = additionalParameters[0];

  // get training data vocab
  ifstream corpusStrme;
  corpusStrme.open(corpusPath.c_str());
  
  Base *obj;
  switch (method) {
    case 1:
      obj = new Simple();
      break;
    case 2:
      obj = new Compound();
      break;
    default:
      abort();
  }
  
  obj->CreateVocab(corpusStrme);
  corpusStrme.close();
    
  for (size_t i = 1; i < additionalParameters.size(); ++i) {
    testPath = additionalParameters[i];
    
    // look up each word in test set
    ifstream testStrme;
    testStrme.open(testPath.c_str());
      
    boost::filesystem::path p(testPath);
    cout << p.filename() << endl;

    obj->CalcOOV(testStrme, outWords);

    testStrme.close();
    
    cout << "----------------------------" << endl;
  }
  
  delete obj;

	cerr << "finished" << endl;
	return 0;
}


