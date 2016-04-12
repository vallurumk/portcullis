//  ********************************************************************
//  This file is part of Portcullis.
//
//  Portcullis is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Portcullis is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Portcullis.  If not, see <http://www.gnu.org/licenses/>.
//  *******************************************************************

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fstream>
#include <string>
#include <iostream>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <vector>
using std::boolalpha;
using std::ifstream;
using std::string;
using std::pair;
using std::map;
using std::unordered_map;
using std::unordered_set;
using std::vector;
using std::cout;
using std::cerr;

#include <Python.h>

#include <boost/algorithm/string.hpp>
#include <boost/exception/all.hpp>
#include <boost/timer/timer.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/variant/recursive_wrapper.hpp>
#include <boost/lexical_cast.hpp>
using boost::timer::auto_cpu_timer;
using boost::lexical_cast;
namespace bfs = boost::filesystem;
namespace po = boost::program_options;
using boost::property_tree::ptree;
namespace qi    = boost::spirit::qi;
namespace phx   = boost::phoenix;

#include <ranger/ForestClassification.h>
#include <ranger/ForestRegression.h>
#include <ranger/DataDouble.h>

#include <portcullis/intron.hpp>
#include <portcullis/junction.hpp>
#include <portcullis/junction_system.hpp>
#include <portcullis/portcullis_fs.hpp>
#include <portcullis/performance.hpp>
using portcullis::PortcullisFS;
using portcullis::Intron;
using portcullis::IntronHasher;
using portcullis::Performance;

#include "train.hpp"
using portcullis::Train;

#include "junction_filter.hpp"


portcullis::Operator portcullis::stringToOp(const string& str) {
    if (boost::iequals(str, "EQ")) {
        return EQ;
    }
    else if (boost::iequals(str, "GT")) {
        return GT;
    }
    else if (boost::iequals(str, "LT")) {
        return LT;
    }
    else if (boost::iequals(str, "GTE")) {
        return GTE;
    }
    else if (boost::iequals(str, "LTE")) {
        return LTE;
    }
    else if (boost::iequals(str, "IN")) {
        return IN;
    }
    else if (boost::iequals(str, "NOT_IN")) {
        return NOT_IN;
    }
    else {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                        "Unrecognised operation: ") + str));
    }
}

string portcullis::opToString(const Operator op) {
    switch (op) {
        case EQ:
            return "EQ";                
        case GT:
            return "GT";
        case LT:
            return "LT";
        case GTE:
            return "GTE";
        case LTE:
            return "LTE";
        case IN:
            return "IN";
        case NOT_IN:
            return "NOT_IN";
        default:
            BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Unrecognised operation")));        
    }    
}

bool portcullis::isNumericOp(Operator op) {
    return (op != IN && op != NOT_IN);
}


portcullis::eval::eval(const NumericFilterMap& _numericmap, const SetFilterMap& _stringmap, const JunctionPtr _junc, JuncResultMap* _juncMap) : 
        boost::static_visitor<bool>() {
    numericmap = _numericmap;
    stringmap = _stringmap;
    junc = _junc;
    juncMap = _juncMap;
}

bool portcullis::eval::operator()(const var& v) const { 

    if (v=="T" || v=="t" || v=="true" || v=="True")
        return true;
    else if (v=="F" || v=="f" || v=="false" || v=="False")
        return false;
    else {
        // If it starts with an M then assume we are looking at a metric
        if (numericmap.count(v) > 0) {
            Operator op = numericmap.at(v).first;
            double threshold = numericmap.at(v).second;
            double value = getNumericFromJunc(v);
            bool res = evalNumberLeaf(op, threshold, value);
            if (!res) {
                juncMap->at(*(junc->getIntron())).push_back(v + " " + opToString(op) + " " + lexical_cast<string>(threshold));
            }
            return res;
        }
        else if (stringmap.count(v) > 0) {
            Operator op = stringmap.at(v).first;
            unordered_set<string> set = stringmap.at(v).second;

            string setstring = boost::algorithm::join(set, ", ");

            string value = getStringFromJunc(v);
            bool res = evalSetLeaf(op, set, value);
            if (!res) {
                juncMap->at(*(junc->getIntron())).push_back(v + " " + opToString(op) + " " + setstring);
            }
            return res;
        }
        else {
            BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                "Unrecognised param: ") + v));
        }

    }
    return boost::lexical_cast<bool>(v); 
}

    
double portcullis::eval::getNumericFromJunc(const var& fullname) const {

    size_t pos = fullname.find(".");

    string name = pos == string::npos ? fullname : fullname.substr(0, pos);

    uint16_t metric_index = -1;
    for(size_t i = 0; i < METRIC_NAMES.size(); i++) {
        if (boost::iequals(name, METRIC_NAMES[i])) {
            metric_index = i;
        }
    }

    if (metric_index == -1) {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                "Unrecognised metric: ") + name));
    }

    switch(metric_index) {
        case 0:
            return 0.0;
        case 1:
            return (double)junc->getNbJunctionAlignments();
        case 2:
            return (double)junc->getNbDistinctAlignments();
        case 3:
            return (double)junc->getNbReliableAlignments();
        case 4:
            return (double)junc->getIntronSize();
        case 5:
            return (double)junc->getLeftAnchorSize();
        case 6:
            return (double)junc->getRightAnchorSize();
        case 7:
            return (double)junc->getMaxMinAnchor();
        case 8:
            return (double)junc->getDiffAnchor();
        case 9:
            return (double)junc->getNbDistinctAnchors();
        case 10:
            return junc->getEntropy();
        case 11:
            return (double)junc->getMaxMMES();
        case 12:
            return (double)junc->getHammingDistance5p();
        case 13:
            return (double)junc->getHammingDistance3p();
        case 14:
            return junc->getCoverage();
        case 15:
            return junc->isUniqueJunction() ? 1.0 : 0.0;
        case 16:
            return junc->isPrimaryJunction() ? 1.0 : 0.0;
        case 17:
            return junc->getMultipleMappingScore();
        case 18:
            return junc->getMeanMismatches();
        case 19:
            return junc->getNbMultipleSplicedReads();
        case 20:
            return junc->getNbUpstreamJunctions();
        case 21:
            return junc->getNbDownstreamJunctions();
        case 22:
            return junc->getNbUpstreamFlankingAlignments();
        case 23:
            return junc->getNbDownstreamFlankingAlignments();

    }


    BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                        "Unrecognised metric")));        
}
    
string portcullis::eval::getStringFromJunc(const var& fullname) const {

    size_t pos = fullname.find(".");

    string name = pos == string::npos ? fullname : fullname.substr(0, pos);

    if (boost::iequals(name, "refname")) {
        return junc->getIntron()->ref.name;
    }
    else if (boost::iequals(name, "M1-canonical_ss")) {
        return string() + cssToChar(junc->getSpliceSiteType());
    }  

    BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                        "Unrecognised param: ") + name));        
}
    
bool portcullis::eval::evalNumberLeaf(Operator op, double threshold, double value) const {
    switch (op) {
        case EQ:
            return value == threshold;                
        case GT:
            return value > threshold;
        case LT:
            return value < threshold;
        case GTE:
            return value >= threshold;
        case LTE:
            return value <= threshold;
        default:
            BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Unrecognised operation")));
    }
}
    
bool portcullis::eval::evalSetLeaf(Operator op, unordered_set<string>& set, string value) const {
    switch (op) {
        case IN:
            return set.find(value) != set.end();
        case NOT_IN:
            return set.find(value) == set.end();
        default:
            BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Unrecognised operation")));
    }
}



portcullis::JunctionFilter::JunctionFilter( const path& _junctionFile, 
                    const path& _output) {
    junctionFile = _junctionFile;
    modelFile = "";
    genuineFile = "";
    output = _output;
    filterFile = "";
    referenceFile = "";
    saveBad = false;
    threads = 1;
    maxLength = 0;
    filterCanonical = false;
    filterSemi = false;
    filterNovel = false;
    source = DEFAULT_FILTER_SOURCE;
    verbose = false;
}
    
    
void portcullis::JunctionFilter::filter() {

    path outputDir = output.parent_path();
    string outputPrefix = output.leaf().string();
    
    if (outputDir.empty()) {
        outputDir = ".";
    }
    
    // Test if provided genome exists
    if (!exists(junctionFile)) {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Could not find junction file at: ") + junctionFile.string()));
    }

    // Test if provided filter config file exists
    if (!modelFile.empty() && !exists(modelFile)) {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Could not find filter model file at: ") + modelFile.string()));
    }
    
    if (!filterFile.empty() && !exists(filterFile)) {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Could not find filter configuration file at: ") + filterFile.string()));
    }
    
    if (!genuineFile.empty() && !exists(genuineFile)) {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Could not find file containing marked junction labels at: ") + genuineFile.string()));
    }
    
    if (!referenceFile.empty() && !exists(referenceFile)) {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Could not find reference BED file at: ") + referenceFile.string()));
    }

    if (!exists(outputDir)) {
        if (!bfs::create_directories(outputDir)) {
            BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Could not create output directory at: ") + outputDir.string()));
        }
    }
    else if (!bfs::is_directory(outputDir)) {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "File exists with name of suggested output directory: ") + outputDir.string()));            
    }
    
    cout << "Loading junctions from " << junctionFile.string() << " ...";
    cout.flush();

    // Load junction system
    JunctionSystem originalJuncs(junctionFile);

    cout << " done." << endl
         << "Found " << originalJuncs.getJunctions().size() << " junctions." << endl << endl;

    unordered_set<string> ref;
    if (!referenceFile.empty()) {        
        ifstream ifs(referenceFile.c_str());

        string line;
        // Loop through until end of file or we move onto the next ref seq
        while ( std::getline(ifs, line) ) {
            boost::trim(line);

            vector<string> parts; // #2: Search for tokens
            boost::split( parts, line, boost::is_any_of("\t"), boost::token_compress_on );

            // Ignore any non-entry lines
            if (parts.size() == 12) {
                int end = std::stoi(parts[7]) - 1;  // -1 to get from BED to portcullis coords for end pos
                string key = parts[0] + "(" + parts[6] + "," + std::to_string(end) + ")" + parts[5];
                ref.insert(key);
            }
        }
    }
    
    vector<bool> genuine;
    if (!genuineFile.empty()) {
        
        cout << "Loading list of correct predictions of performance analysis ...";
        cout.flush();
        
        Performance::loadGenuine(genuineFile, genuine);
        
        if (genuine.size() != originalJuncs.getJunctions().size()) {
            BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Genuine file contains ") + lexical_cast<string>(genuine.size()) +
                    " entries.  Junction file contains " + lexical_cast<string>(originalJuncs.getJunctions().size()) + 
                    " junctions.  The number of entries in both files must be the same to assess performance."));
        }
        
        // Copy over results into junction list
        for(size_t i = 0; i < originalJuncs.getJunctions().size(); i++) {
            originalJuncs.getJunctionAt(i)->setGenuine(genuine[i]);
        }
        
        cout << " done." << endl << endl;
    }
    
    // Also keep the current list of junctions
    JunctionList currentJuncs;
    
    // Copy everything into passJunc to begin with
    for(auto& j : originalJuncs.getJunctions()) {
        currentJuncs.push_back(j);
    }
    
    if (train) {
        
        JunctionList initialSet;
        uint32_t pos = 0;
        uint32_t neg = 0;
        
         
        cout << "Collecting initial positive and negative sets from input ...";
        cout.flush();
        
        // Guess at some likely initial positive and negative junctions
        for(auto& j : currentJuncs) {
            if (    j->getMeanMismatches() < 1.0 &&
                    j->getEntropy() > 3.0 &&
                    j->getHammingDistance3p() > 8 &&
                    j->getHammingDistance5p() > 8 &&
                    j->getMaxMMES() > 20 &&
                    j->getNbReliableAlignments() > 5) {
                
                JunctionPtr copy = make_shared<Junction>(*j);
                copy->setGenuine(true);
                initialSet.push_back(copy);
                pos++;
            }
            else if (   (j->getNbJunctionAlignments() == 1 &&
                        j->getMaxMinAnchor() < 8) ||
                        j->isPotentialFalsePositive()) {
                JunctionPtr copy = make_shared<Junction>(*j);
                copy->setGenuine(false);
                initialSet.push_back(j);
                neg++;
            }            
        }
        
        cout << " done." << endl
             << "Found " << pos << " initial positive junctions and " << neg << " negative junctions, which will be used for training." << endl;

        shared_ptr<Forest> forest = Train::trainInstance(initialSet, output.string() + ".selftrain", 100, threads, true, true);
        
        forest->saveToFile();
        forest->writeOutput(&cout);
        
        modelFile = output.string() + ".selftrain.forest";
    }
    
    
    
    
    // Manage a junction system of all discarded junctions
    JunctionSystem discardedJuncs;
    JunctionSystem refKeptJuncs;
    
    // Do ML based filtering if requested
    if(!modelFile.empty() && exists(modelFile)){
        cout << "Predicting valid junctions using random forest model ...";
        cout.flush();
        
        JunctionList passJuncs;
        JunctionList failJuncs;
        JunctionList refJuncs;
        
        forestPredict(currentJuncs, passJuncs, failJuncs, refJuncs, ref);
        
        cout << " done." << endl << endl;
        
        printFilteringResults(currentJuncs, passJuncs, failJuncs, refJuncs, string("Random Forest filtering"));
        
        // Reset currentJuncs
        currentJuncs.clear();
        for(auto& j : passJuncs) {
            currentJuncs.push_back(j);
        }
        
        for(auto& j : failJuncs) {
            discardedJuncs.addJunction(j);
        }
        
        for(auto& j : refJuncs) {
            refKeptJuncs.addJunction(j);
        }
    }
    
    
    // Do rule based filtering if requested
    if (!filterFile.empty() && exists(filterFile)) {        
        
        JunctionList passJuncs;
        JunctionList failJuncs;
        JunctionList refJuncs;
                
        cout << "Loading JSON rule-based filtering config file ...";

        ptree pt;
        boost::property_tree::read_json(filterFile.string(), pt);

        cout << " done." << endl << endl;

        cout << "Filtering junctions ...";
        cout.flush();

        NumericFilterMap numericFilters;
        SetFilterMap stringFilters;
        JuncResultMap junctionResultMap;

        for(ptree::value_type& v : pt.get_child("parameters")) {
            string name = v.first;
            Operator op = stringToOp(v.second.get_child("operator").data());
            if (isNumericOp(op)) {
                double threshold = lexical_cast<double>(v.second.get_child("value").data());     
                numericFilters[name] = pair<Operator,double>(op, threshold);
            }
            else {

                unordered_set<string> set;
                for (auto& item : v.second.get_child("value")) {
                    string val = item.second.get_value<string>();
                    set.insert(val); 
                }
                stringFilters[name] = pair<Operator,unordered_set<string>>(op, set);
            }
        }

        const string expression = pt.get_child("expression").data();

        map<string,int> filterCounts;

        for (JunctionPtr junc : originalJuncs.getJunctions()) {

            junctionResultMap[*(junc->getIntron())] = vector<string>();

            if (parse(expression, junc, numericFilters, stringFilters, &junctionResultMap)) {
                passJuncs.push_back(junc);
            }
            else if (!referenceFile.empty() && ref.count(junc->locationAsString()) > 0) {
                passJuncs.push_back(junc);
                refJuncs.push_back(junc);
            }
            else {
                failJuncs.push_back(junc);
                discardedJuncs.addJunction(junc);

                vector<string> failed = junctionResultMap[*(junc->getIntron())];

                for(string s : failed) {
                    filterCounts[s]++;
                }
            }
        }        

        cout << " done." << endl << endl
             << "Number of junctions failing for each filter: " << endl;

        for(map<string,int>::iterator iterator = filterCounts.begin(); iterator != filterCounts.end(); iterator++) {        
            cout << iterator->first << ": " << iterator->second << endl;
        }
        
        saveResults(originalJuncs, junctionResultMap);
        
        printFilteringResults(currentJuncs, passJuncs, failJuncs, refJuncs, string("Rule-based filtering"));
        
        // Reset currentJuncs
        currentJuncs.clear();
        for(auto& j : passJuncs) {
            currentJuncs.push_back(j);
        }
    }
    
    if (maxLength > 0 || this->doCanonicalFiltering()) {
        
        JunctionList passJuncs;
        JunctionList failJuncs;
        JunctionList refJuncs;
        
        for(auto& j : currentJuncs) {
            
            bool pass = true;
            if (maxLength > 0) {
                if (j->getIntronSize() > maxLength) {
                    pass = false;
                }
            }
            
            if (pass && this->doCanonicalFiltering()) {
                if (this->filterNovel && j->getSpliceSiteType() == CanonicalSS::NO) {
                    pass = false;
                }
                if (this->filterSemi && j->getSpliceSiteType() == CanonicalSS::SEMI_CANONICAL) {
                    pass = false;
                }
                if (this->filterCanonical && j->getSpliceSiteType() == CanonicalSS::CANONICAL) {
                    pass = false;
                }
            }

            if (pass) {
                passJuncs.push_back(j);
            }
            else if (!referenceFile.empty() && ref.count(j->locationAsString()) > 0) {
                passJuncs.push_back(j);
                refJuncs.push_back(j);
            }
            else {
                failJuncs.push_back(j);
                discardedJuncs.addJunction(j);
            }
        }
        
        printFilteringResults(currentJuncs, passJuncs, failJuncs, refJuncs, string("Post filtering (length and/or canonical)"));
        
        // Reset currentJuncs
        currentJuncs.clear();
        for(auto& j : passJuncs) {
            currentJuncs.push_back(j);
        }
    }
    
    JunctionSystem filteredJuncs;
        
    if (currentJuncs.empty()) {
        cout << "WARNING: Filters discarded all junctions from input." << endl;
    }
    else {
    
        cout  << "Recalculating junction grouping and distance stats based on new junction list that passed filters ...";
        cout.flush();

        for(auto& j : currentJuncs) {
            filteredJuncs.addJunction(j);
        }

        filteredJuncs.calcJunctionStats();

        cout << " done." << endl << endl;
    }
    
    printFilteringResults(  originalJuncs.getJunctions(), 
                            filteredJuncs.getJunctions(), 
                            discardedJuncs.getJunctions(), 
                            refKeptJuncs.getJunctions(), 
                            string("Overall results"));

    cout << "Saving junctions passing filter to disk:" << endl;

    filteredJuncs.saveAll(outputDir.string() + "/" + outputPrefix + ".pass", source + "_pass");
    
    if (saveBad) {
        cout << "Saving junctions failing filter to disk:" << endl;

        discardedJuncs.saveAll(outputDir.string() + "/" + outputPrefix + ".fail", source + "_fail");
        
        if (!referenceFile.empty()) {
            cout << "Saving junctions failing filters but present in reference:" << endl;

            refKeptJuncs.saveAll(outputDir.string() + "/" + outputPrefix + ".ref", source + "_ref");
        }
    }
}
    
void portcullis::JunctionFilter::printFilteringResults(const JunctionList& in, const JunctionList& pass, const JunctionList& fail, const JunctionList& ref, string prefix) {
    // Output stats
    size_t diff = in.size() - pass.size();

    cout << prefix << endl
         << "-------------------------" << endl
         << "Input contained " << in.size() << " junctions." << endl
         << "Output contains " << pass.size() << " junctions." << endl
         << "Filtered out " << diff << " junctions." << endl;
    
    if (!referenceFile.empty()) {
        cout << ref.size() << " junctions would have been discarded but are retained due to presence in reference" << endl;
    }
    
    if (!genuineFile.empty() && exists(genuineFile)) {
        calcPerformance(pass, fail);
    }
}

void portcullis::JunctionFilter::calcPerformance(const JunctionList& pass, const JunctionList& fail) {
    
    uint32_t tp = 0, tn = 0, fp = 0, fn = 0;

    for(auto& j : pass) {
        if (j->isGenuine()) tp++; else fp++;
    }
    
    for(auto& j : fail) {
        if (!j->isGenuine()) tn++; else fn++;
    }
    
    Performance p(tp, tn, fp, fn);
    cout << Performance::longHeader() << endl;
    cout << p.toLongString() << endl << endl;
}

void portcullis::JunctionFilter::saveResults(const JunctionSystem& js, JuncResultMap& results) {

    // Print descriptive output to file
    ofstream out(output.string() + ".rule_filtering.results");

    out << Intron::locationOutputHeader() << "\tconsensus_strand\t" << "filter_results..." << endl;

    for(auto& kv: results) {

        Intron i = kv.first;

        out << i << "\t";

        out << strandToChar(js.getJunction(i)->getConsensusStrand()) << "\t";
        
        if (kv.second.empty()) {
            out << "PASS";
        }
        else {
            out << boost::algorithm::join(kv.second, "\t");
        }
        out << endl;
    }

    out.close();
}
    
    
/**
 * This function evaluates the truth status of a row parameter given the configuration present in the JSON file.
 * @param op Operation to be considered
 * @param threshold Threshold
 * @param param Value
 * @return True if parameter passes operation and threshold, false otherwise
 */
bool portcullis::JunctionFilter::parse(const string& expression, JunctionPtr junc, NumericFilterMap& numericFilters, SetFilterMap& stringFilters, JuncResultMap* results) {

    typedef std::string::const_iterator it;
    it f(expression.begin()), l(expression.end());
    parser<it> p;

    expr result;
    bool ok = qi::phrase_parse(f,l,p,qi::space,result);

    if (!ok) {
        BOOST_THROW_EXCEPTION(JuncFilterException() << JuncFilterErrorInfo(string(
                    "Invalid expression: ") + expression));
    }

    // Evaluate results
    return boost::apply_visitor(eval(numericFilters, stringFilters, junc, results), result);
}
    
wchar_t* portcullis::JunctionFilter::convertCharToWideChar(const char* c) {
    const size_t cSize = strlen(c)+1;
    wchar_t* wc = new wchar_t[cSize];
    mbstowcs (wc, c, cSize);    
    return wc;
}       

void portcullis::JunctionFilter::executePythonMLFilter(const path& mlOutputFile) {
    
    const path script_name = "ml_filter.py";
    const path scripts_dir = JunctionFilter::scriptsDir;
    const path full_script_path = path(scripts_dir.string() + "/" + script_name.string());
    
    stringstream ss;
    
    // Create wide char alternatives
    wchar_t* wsn = convertCharToWideChar(script_name.c_str());
    wchar_t* wsp = convertCharToWideChar(full_script_path.c_str());    
    wchar_t* wargv[10]; // Can't use variable length arrays!
    wargv[0] = wsp;
    ss << full_script_path.c_str();
    string model = string("--model=") + modelFile.string();
    wargv[1] = convertCharToWideChar(model.c_str());
    ss << " " << model;
    string mloFile = string("--output=") + mlOutputFile.string();
    wargv[2] = convertCharToWideChar(mloFile.c_str());
    ss << " " << mloFile;
    wargv[3] = convertCharToWideChar(junctionFile.c_str());
    ss << " " << junctionFile.string();
        
    if (verbose) {
        cout << endl << "Effective command line: " << ss.str() << endl << endl;
    }

    std::ifstream script_in(full_script_path.c_str());
    std::string contents((std::istreambuf_iterator<char>(script_in)), std::istreambuf_iterator<char>());

    // Run python script
    Py_Initialize();
    Py_SetProgramName(wsp);
    PySys_SetArgv(4, wargv);
    PyRun_SimpleString(contents.c_str());
    Py_Finalize();

    // Cleanup
    delete wsn;
    // No need to free up "wsp" as it is element 0 in the array
    for(int i = 0; i < 4; i++) {
        delete wargv[i];
    }
}


void portcullis::JunctionFilter::forestPredict(const JunctionList& all, JunctionList& pass, JunctionList& fail, JunctionList& refs, const unordered_set<string>& ref) {
    
    if (verbose) {
        cerr << endl << "Preparing junction metrics into matrix" << endl;
    }
    
    Data* testingData = Train::juncs2FeatureVectors(all);
    
    // Load forest from disk
    
    if (verbose) {
        cerr << "Initialising random forest" << endl;
    }
    
    shared_ptr<Forest> f = nullptr;
    if (train) {
        f = make_shared<ForestRegression>();
    }
    else {
        f = make_shared<ForestClassification>();
    }
    
    vector<string> catVars;
    
    f->init(
        "Genuine",                  // Dependant variable name
        MEM_DOUBLE,                 // Memory mode
        testingData,                // Data object
        0,                          // M Try (0 == use default)
        "",                         // Output prefix 
        DEFAULT_TRAIN_TREES,        // Number of trees (will be overwritten when loading the model)
        DEFAULT_SEED,               
        threads,                    // Number of threads
        IMP_GINI,                   // Importance measure 
        train ? DEFAULT_MIN_NODE_SIZE_REGRESSION : DEFAULT_MIN_NODE_SIZE_CLASSIFICATION,  // Min node size
        "",                         // Status var name 
        true,                       // Prediction mode
        true,                       // Replace 
        catVars,                    // Unordered categorical variable names (vector<string>)
        false,                      // Memory saving
        DEFAULT_SPLITRULE,          // Split rule
        false,                      // predall
        1.0);                       // Sample fraction
    
    f->setVerboseOut(&cerr);
    
    if (verbose) {
        cerr << "Loading forest model from disk" << endl;
    }
    
    // Load trees from saved model
    f->loadFromFile(modelFile.string());
    
    if (verbose) {
        cerr << "Running predictions" << endl;
    }
    f->run(verbose);
    
    if (verbose) {
        cerr << "Separating original junction data into pass and fail categories" << endl;
    }
    for(size_t i = 0; i < all.size(); i++) {
        if (f->getPredictions()[i][0] >= 1.0) {
            pass.push_back(all[i]);
        }
        else if (!referenceFile.empty() && ref.count(all[i]->locationAsString()) > 0) {
            pass.push_back(all[i]);
            refs.push_back(all[i]);
        }
        else {
            fail.push_back(all[i]);
        }
    }
    
    delete testingData;
}

int portcullis::JunctionFilter::main(int argc, char *argv[]) {

    path junctionFile;
    path modelFile;
    path genuineFile;
    path filterFile;
    path referenceFile;
    path output;
    uint16_t threads;
    bool saveBad;
    bool train;
    bool no_ml;
    int32_t max_length;
    string canonical;
    string source;
    bool verbose;
    bool help;
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);


    // Declare the supported options.
    po::options_description generic_options(helpMessage(), w.ws_col, w.ws_col/1.7);
    generic_options.add_options()
            ("output,o", po::value<path>(&output)->default_value(DEFAULT_FILTER_OUTPUT), 
                "Output prefix for files generated by this program.")
            ("filter_file,f", po::value<path>(&filterFile), 
                "If you wish to custom rule-based filter the junctions file, use this option to provide a list of the rules you wish to use.  By default we don't filter using a rule-based method, we instead filter via a random forest model.  See manual for more details.")
            ("model_file,m", po::value<path>(&modelFile)->default_value(defaultModelFile), 
                "If you wish to use a custom random forest model to filter the junctions file, use this option to. See manual for more details.")
            ("train", po::bool_switch(&train)->default_value(false),
                "Trains a random forest model based on the input data provided, then filters based on the regression results from the model.")
            ("genuine,g", po::value<path>(&genuineFile),
                "If you have a list of line separated boolean values in a file, indicating whether each junction in your input is genuine or not, then we can use that information here to gauge the accuracy of the predictions.")
            ("reference,r", po::value<path>(&referenceFile),
                "Reference annotation of junctions in BED format.  Any junctions found by the junction analysis tool will be preserved if found in this reference file regardless of any other filtering criteria.  If you need to convert a reference annotation from GTF or GFF to BED format portcullis contains scripts for this.")
            ("save_bad,b", po::bool_switch(&saveBad)->default_value(false),
                "Saves bad junctions (i.e. junctions that fail the filter), as well as good junctions (those that pass)")
            ("source", po::value<string>(&source)->default_value(DEFAULT_FILTER_SOURCE),
                "The value to enter into the \"source\" field in GFF files.")
            ("no_ml,n", po::bool_switch(&no_ml)->default_value(false),
                "Whether or not to disable random forest prediction")
            ("max_length,l", po::value<int32_t>(&max_length)->default_value(0),
                "Filter junctions longer than this value.  Default (0) is to not filter based on length.")
            ("canonical,c", po::value<string>(&canonical)->default_value("OFF"),
                "Keep junctions based on their splice site status.  Valid options: OFF,C,S,N. Where C = Canonical junctions (GU-AG), S = Semi-canonical junctions (AT-AC, or  GT-AG), N = Non-canonical.  OFF means, keep all junctions (i.e. don't filter by canonical status).  User can separate options by a comma to keep two categories.")
            ("threads,t", po::value<uint16_t>(&threads)->default_value(DEFAULT_FILTER_THREADS), 
                "The number of threads to use during testing (only applies if using forest model).")
            ("verbose,v", po::bool_switch(&verbose)->default_value(false), 
                "Print extra information")
            ("help", po::bool_switch(&help)->default_value(false), "Produce help message")
            ;

    // Hidden options, will be allowed both on command line and
    // in config file, but will not be shown to the user.
    po::options_description hidden_options("Hidden options");
    hidden_options.add_options()
            ("junction-file", po::value<path>(&junctionFile), "Path to the junction file to process.")
            ;

    // Positional option for the input bam file
    po::positional_options_description p;
    p.add("junction-file", 1);


    // Combine non-positional options
    po::options_description cmdline_options;
    cmdline_options.add(generic_options).add(hidden_options);

    // Parse command line
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);
    po::notify(vm);

    // Output help information the exit if requested
    if (help || argc <= 1) {
        cout << generic_options << endl;
        return 1;
    }



    auto_cpu_timer timer(1, "\nPortcullis junction filter completed.\nTotal runtime: %ws\n\n");        

    cout << "Running portcullis in junction filter mode" << endl
         << "------------------------------------------" << endl << endl;

    // Create the prepare class
    JunctionFilter filter(junctionFile, output);
    filter.setSaveBad(saveBad);
    filter.setSource(source);
    filter.setVerbose(verbose);
    filter.setThreads(threads);
    filter.setMaxLength(max_length);
    filter.setCanonical(canonical);
    
        
    // Only set the filter rules if specified.
    filter.setFilterFile(filterFile);
    filter.setGenuineFile(genuineFile);
    filter.setTrain(train);
    if (!no_ml && !train) {
        filter.setModelFile(modelFile);
    }
    filter.setReferenceFile(referenceFile);
    
    filter.filter();

    return 0;
}

path portcullis::JunctionFilter::scriptsDir = "";
path portcullis::JunctionFilter::defaultModelFile = DEFAULT_FILTER_MODEL_FILE;
path portcullis::JunctionFilter::defaultFilterFile = DEFAULT_FILTER_RULE_FILE;

