/*
 *  synthetictest.cpp
 *  @author Daniel Ayres
 *  Based on tinyTest.cpp by Andrew Rambaut, genomictest.cpp by Aaron Darling.
 *  PLL comparison based on test files by Diego Darriba and Tomas Flouri
 */
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <stack>
#include <queue>
#include <cstdarg>
#include <thread>
#include <future>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <functional>

#ifdef _WIN32
    #include <winsock.h>
    #include <string>
#else
    #include <sys/time.h>
#endif

#include "libhmsbeagle/beagle.h"
#include "linalg.h"

#ifdef HAVE_NCL
    #include "ncl/nxsmultiformat.h"
    #include <regex>
#endif // HAVE_NCL

#ifdef HAVE_PLL
    #include "libpll/pll.h"
#endif // HAVE_PLL


#define MAX_DIFF    0.01        //max discrepancy in scoring between reps
#define GT_RAND_MAX 0x7fffffff

#ifdef _WIN32
    //From January 1, 1601 (UTC). to January 1,1970
    #define FACTOR 0x19db1ded53e8000 

    int gettimeofday(struct timeval *tp,void * tz) {
        FILETIME f;
        ULARGE_INTEGER ifreq;
        LONGLONG res; 
        GetSystemTimeAsFileTime(&f);
        ifreq.HighPart = f.dwHighDateTime;
        ifreq.LowPart = f.dwLowDateTime;

        res = ifreq.QuadPart - FACTOR;
        tp->tv_sec = (long)((LONGLONG)res/10000000);
        tp->tv_usec =(long)(((LONGLONG)res%10000000)/10); // Micro Seconds

        return 0;
    }
#endif

double cpuTimeSetPartitions, cpuTimeUpdateTransitionMatrices, cpuTimeUpdatePartials, cpuTimeAccumulateScaleFactors, cpuTimeCalculateRootLogLikelihoods, cpuTimeTotal;

bool useStdlibRand;

static unsigned int rand_state = 1;

int gt_rand_r(unsigned int *seed)
{
    *seed = *seed * 1103515245 + 12345;
    return (*seed % ((unsigned int)GT_RAND_MAX + 1));
}

int gt_rand(void)
{
    if (!useStdlibRand) {
        return (gt_rand_r(&rand_state));
    } else {
        return rand();
    }
}

void gt_srand(unsigned int seed)
{
    if (!useStdlibRand) {
        rand_state = seed;
    } else {
        srand(seed);
    }
}

void abort(std::string msg) {
    std::cerr << msg << "\nAborting..." << std::endl;
    std::exit(1);
}

void abortf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    std::cerr << "\nAborting..." << std::endl;        \
    std::exit(1);
}


double* getRandomTipPartials( int nsites, int stateCount )
{
    double *partials = (double*) calloc(sizeof(double), nsites * stateCount); // 'malloc' was a bug
    for( int i=0; i<nsites*stateCount; i+=stateCount )
    {
        int s = gt_rand()%stateCount;
        // printf("%d ", s);
        partials[i+s]=1.0;
    }
    return partials;
}

int* getRandomTipStates( int nsites, int stateCount )
{
    int *states = (int*) calloc(sizeof(int), nsites); 
    for( int i=0; i<nsites; i++ )
    {
        int s = gt_rand()%stateCount;
        states[i]=s;
    }
    return states;
}

struct threadData
{
    std::thread t; // The thread object
    std::queue<std::packaged_task<void()>> jobs; // The job queue
    std::condition_variable cv; // The condition variable to wait for threads
    std::mutex m; // Mutex used for avoiding data races
    bool stop = false; // When set, this flag tells the thread that it should exit
};

struct node
{
    int data;
    double edge;
    struct node* left;
    struct node* right;
    struct node* parent;
};


node* createNewNode(int data)
{
    node* temp = new node;
    temp->data = data;
    temp->edge = 0.0;
    temp->left = NULL;
    temp->right = NULL;
    temp->parent = NULL;
 
    return (temp);
}

#ifdef HAVE_NCL

typedef std::vector<double>             pattern_counts_t;
typedef std::vector<int>                pattern_t;
typedef std::map< pattern_t, unsigned > pattern_map_t;
typedef std::vector< pattern_t >        data_matrix_t;

pattern_map_t                           _pattern_map;       // used as workspace
data_matrix_t                           _data_matrix;
pattern_counts_t                        _pattern_counts;

NxsTaxaBlock* taxaBlock;
NxsCharactersBlock* charBlock;

// code adapted from NCL documentation 
//  (http://phylo.bio.ku.edu/ncldocs/v2.1/funcdocs/index.html)
// and Phylogenetic Software Development Tutorial (version 2)
//  (Paul O. Lewis Laboratory, https://phylogeny.uconn.edu/tutorial-v2)

void ncl_readAlignmentDNA(char* filename, int* ntaxa, int* nsites, bool compress)
{
    MultiFormatReader nexusReader(-1, NxsReader::IGNORE_WARNINGS);
    nexusReader.ReadFilepath(filename, MultiFormatReader::RELAXED_PHYLIP_DNA_FORMAT);

    taxaBlock = nexusReader.GetTaxaBlock(0);
    charBlock = nexusReader.GetCharactersBlock(taxaBlock, 0);

    *ntaxa = taxaBlock->GetNTax();
    *nsites = charBlock->GetNCharTotal();

    std::cout << "\nReading DNA alignment from file " << filename;
    std::cout << ", with " << *ntaxa << " taxa and " << *nsites << " sites";

    unsigned ntax = *ntaxa;

    _data_matrix.resize(ntax);

    for (unsigned t = 0; t < ntax; ++t) {
        const NxsDiscreteStateRow & row = charBlock->GetDiscreteMatrixRow(t);
        unsigned seqlen = (unsigned)row.size();
        _data_matrix[t].resize(seqlen);
        unsigned k = 0;
        for (auto state_code : row) {
            if (state_code < 0 || state_code > 3) {
                _data_matrix[t][k++] = 4; 
            } else {
                _data_matrix[t][k++] = state_code;
            }
        }
    }

    if (compress) {
        // create map with keys equal to patterns and values equal to site counts
        _pattern_map.clear();

        std::vector<int> pattern;
        unsigned numtaxa = (unsigned)_data_matrix.size();
        unsigned seqlen = (unsigned)_data_matrix[0].size();
        for (unsigned i = 0; i < seqlen; ++i)
            {
            // Create vector representing pattern at site i
            pattern.clear();
            for (unsigned j = 0; j < numtaxa; ++j)
                {
                pattern.push_back(_data_matrix[j][i]);
                }

            // Add this pattern to pattern_counts
            // If pattern is not already in pattern_map, insert it and set value to 1.
            // If it does exist, increment its current value.
            // (see item 24, p. 110, in Meyers' Efficient STL for more info on the technique used here)
            pattern_map_t::iterator lowb = _pattern_map.lower_bound(pattern);
            if (lowb != _pattern_map.end() && !(_pattern_map.key_comp()(pattern, lowb->first)))
                {
                // this pattern has already been seen
                lowb->second += 1;
                }
            else
                {
                // this pattern has not yet been seen
                _pattern_map.insert(lowb, pattern_map_t::value_type(pattern, 1));
                }
            }
            
        // resize _pattern_counts
        unsigned npatterns = (unsigned)_pattern_map.size();
        _pattern_counts.resize(npatterns);
        
        // resize _data_matrix so that we can use operator[] to assign values
        _data_matrix.resize(numtaxa);
        for (auto & row : _data_matrix)
            {
            row.resize(npatterns);
            }

        unsigned j = 0;
        for (auto & pc : _pattern_map)
            {
            _pattern_counts[j] = pc.second;

            unsigned i = 0;
            for (auto sc : pc.first)
                {
                _data_matrix[i][j] = sc;
                ++i;
                }

            ++j;
            }

        // Everything has been transferred to _data_matrix and _pattern_counts, so can now free this memory
        _pattern_map.clear();

        std::cout << " (" << npatterns << " unique patterns)";

        *nsites = npatterns;
    }
}

int* ncl_getAlignmentTipStates( int nsites, int taxa)
{
    int *states = (int*) calloc(sizeof(int), nsites); 
    for( int i=0; i<nsites; i++ )
    {
        states[i]= _data_matrix[taxa][i];
    }
    return states;
}


void ncl_generateTreeFromNewick(char* filename, int ntaxa, std::vector <node*> &nodes, node* root)
{
    bool rooted = true;

    // readTreefile
    MultiFormatReader nexusReader(-1, NxsReader::IGNORE_WARNINGS);
    try {
        nexusReader.ReadFilepath(filename, MultiFormatReader::RELAXED_PHYLIP_TREE_FORMAT);
        }
    catch(...)
        {
        nexusReader.DeleteBlocksFromFactories();
        abort("Error reading Newick file");
        }
    const NxsTreesBlock * treesBlock = nexusReader.GetTreesBlock(nexusReader.GetTaxaBlock(0), 0);
    const NxsFullTreeDescription & d = treesBlock->GetFullTreeDescription(0);
    // store the newick tree description
    std::string raw_newick = d.GetNewick();
    nexusReader.DeleteBlocksFromFactories();

    // stripOutNexusComments
    std::regex commentexpr("\\[.*?\\]");
    std::string newick = std::regex_replace(raw_newick, commentexpr, std::string(""));

    // countNewickLeaves
    std::regex taxonexpr("[(,]\\s*(\\d+|\\S+?|['].+?['])\\s*(?=[,):])");
    std::sregex_iterator m1(newick.begin(), newick.end(), taxonexpr);
    std::sregex_iterator m2;
    int ntaxa_newick = (unsigned)std::distance(m1, m2);

    if (ntaxa_newick != ntaxa) {
        abortf("Wrong number of taxa in Newick file (%d != %d)", ntaxa_newick, ntaxa);
    }

    // CHECK if 0 should be 1
    unsigned max_nodes = 2*ntaxa - (rooted ? 0 : 2);

    // std::vector <node*> nodes;

    std::set<unsigned> used; // used to ensure that two tips do not have the same number
    unsigned curr_leaf = 0;
    node* first_tip = NULL;

    unsigned num_edge_lengths = 0;
    unsigned curr_node_index = 0;
    unsigned curr_internal_node = ntaxa;

    // Root node
    node* nd = root;
    root->data = curr_node_index;
    nodes.push_back(root);

    // if (rooted)
    //     {
    //     curr_node_index++;
    //     nd = createNewNode(curr_node_index);
    //     nodes.push_back(nd);    
    //     nd->parent = root;
    //     nd->parent->left = nd;
    //     }

    // Some flags to keep track of what we did last
    enum {
        Prev_Tok_LParen     = 0x01, // previous token was a left parenthesis ('(') 
        Prev_Tok_RParen     = 0x02, // previous token was a right parenthesis (')') 
        Prev_Tok_Colon      = 0x04, // previous token was a colon (':') 
        Prev_Tok_Comma      = 0x08, // previous token was a comma (',') 
        Prev_Tok_Name       = 0x10, // previous token was a node name (e.g. '2', 'P._articulata')
        Prev_Tok_EdgeLen    = 0x20  // previous token was an edge length (e.g. '0.1', '1.7e-3') 
        };
    unsigned previous = Prev_Tok_LParen;


    // Some useful flag combinations 
    unsigned LParen_Valid = (Prev_Tok_LParen | Prev_Tok_Comma);
    unsigned RParen_Valid = (Prev_Tok_RParen | Prev_Tok_Name | Prev_Tok_EdgeLen);
    unsigned Comma_Valid  = (Prev_Tok_RParen | Prev_Tok_Name | Prev_Tok_EdgeLen);
    unsigned Colon_Valid  = (Prev_Tok_RParen | Prev_Tok_Name);
    unsigned Name_Valid   = (Prev_Tok_RParen | Prev_Tok_LParen | Prev_Tok_Comma);

    // Set to true while reading an edge length
    bool inside_edge_length = false;
    std::string edge_length_str;
    unsigned edge_length_position = 0;

    // Set to true while reading a node name surrounded by (single) quotes
    bool inside_quoted_name = false;

    // Set to true while reading a node name not surrounded by (single) quotes
    bool inside_unquoted_name = false;

    // Set to start of each node name and used in case of error
    unsigned node_name_position = 0;

    // loop through the characters in newick, building up tree as we go
    unsigned position_in_string = 0;

    for (auto ch : newick)
        {
        position_in_string++;

        if (inside_quoted_name)
            {
            if (ch == '\'')
                {
                inside_quoted_name = false;
                node_name_position = 0;
                if (!nd->left)
                    {
                    // extractNodeNumberFromName(nd, used);
                    if (nd->data != curr_leaf)
                        {
                        node* tmp_node;
                        for (auto n : nodes)
                            {
                            if (n->data == curr_leaf)
                                {
                                tmp_node = n;
                                break;
                                }
                            }
                        tmp_node->data = nd->data;
                        nd->data = curr_leaf; 
                        }
                    curr_leaf++;
                    if (!first_tip)
                        first_tip = nd;
                    }

                previous = Prev_Tok_Name;
                }
            // else if (iswspace(ch))
            //     nd->_name += ' ';
            // else
            //     nd->_name += ch;

            continue;
            }
            else if (inside_unquoted_name)
                {
                if (ch == '(')
                    abortf("Unexpected left parenthesis inside node name at position %d in tree description", node_name_position);

                if (iswspace(ch) || ch == ':' || ch == ',' || ch == ')')
                    {
                    inside_unquoted_name = false;

                    // Expect node name only after a left paren (child's name), a comma (sib's name) or a right paren (parent's name)
                    if (!(previous & Name_Valid))
                        abortf("Unexpected node name at position %d in tree description", node_name_position);

                    if (!nd->left)
                        {
                        // extractNodeNumberFromName(nd, used);
                        if (nd->data != curr_leaf)
                            {
                            node* tmp_node;
                            for (auto n : nodes)
                                {
                                if (n->data == curr_leaf)
                                    {
                                    tmp_node = n;
                                    break;
                                    }
                                }
                            tmp_node->data = nd->data;
                            nd->data = curr_leaf; 
                            }
                        curr_leaf++;
                        if (!first_tip)
                            first_tip = nd;
                        }

                    previous = Prev_Tok_Name;
                    }
                else
                    {
                    // nd->_name += ch;
                    continue;
                    }
                }
        else if (inside_edge_length)
            {
            if (ch == ',' || ch == ')' || iswspace(ch))
                {
                inside_edge_length = false;
                edge_length_position = 0;
                // extractEdgeLen(nd, edge_length_str);
                double d = 0.0;
                try
                    {
                    d = std::stof(edge_length_str);
                    }
                catch(std::invalid_argument &)
                    {
                    // edge_length_string could not be converted to a double value
                    abortf("%s is not interpretable as an edge length", edge_length_str.c_str());
                    }
                // conversion succeeded
                nd->edge = (d < 0.0 ? 0.0 : d);
                ++num_edge_lengths;
                previous = Prev_Tok_EdgeLen;
                }
            else
                {
                bool valid = (ch =='e' || ch == 'E' || ch =='.' || ch == '-' || ch == '+' || isdigit(ch));
                if (!valid)
                    abortf("Invalid branch length character (%c) at position %d in tree description", ch, position_in_string);

                edge_length_str += ch;
                continue;
                }
            }

        if (iswspace(ch))
            continue;

        switch(ch)
            {
            case ';':
                break;

            case ')':
                // If nd is bottommost node, expecting left paren or semicolon, but not right paren
                if (!nd->parent)
                    abortf("Too many right parentheses at position %d in tree description", position_in_string);

                // Expect right paren only after an edge length, a node name, or another right paren
                if (!(previous & RParen_Valid))
                    abortf("Unexpected right parenthesisat position %d in tree description", position_in_string);

                // Go down a level
                nd = nd->parent;
                if (!nd->right)
                    abortf("Internal node has only one child at position %d in tree description", position_in_string);
                previous = Prev_Tok_RParen;
                break;

            case ':':
                // Expect colon only after a node name or another right paren
                if (!(previous & Colon_Valid))
                    abortf("Unexpected colon at position %d in tree description", position_in_string);
                previous = Prev_Tok_Colon;
                break;

            case ',':
                // Expect comma only after an edge length, a node name, or a right paren
                if (!nd->parent || !(previous & Comma_Valid))
                    abortf("Unexpected comma at position %d in tree description", position_in_string);

                // Check for polytomies
                {
                bool nd_can_have_sibling = true;
                if (!nd->parent)
                    {
                    // trying to give root node a sibling
                    nd_can_have_sibling = false;
                    }

                if (nd != nd->parent->left)
                    {
                    if (nd->parent->parent)
                        {
                        // trying to give a sibling to a sibling of nd, and nd's parent is not the root
                        nd_can_have_sibling = false;
                        }
                    else
                        {
                        if (rooted)
                            {
                            // root node has exactly 2 children in rooted trees
                            nd_can_have_sibling = false;
                            }
                        else if (nd != nd->parent->right)
                            {
                            // trying to give root node more than 3 children
                            nd_can_have_sibling = false;
                            }
                        }
                    }

                    if (!nd_can_have_sibling)
                        abortf("Polytomy found in the following tree description but polytomies prohibited:\n%s", newick.c_str());
                }

                // Create the sibling
                curr_node_index++;
                if (curr_node_index == max_nodes)
                    abortf("Wrong number of nodes specified by tree description (%d nodes allocated for %d leaves)", max_nodes, ntaxa);
                nd->parent->right = createNewNode(curr_node_index);
                nd->parent->right->parent = nd->parent;
                nd = nd->parent->right;
                nodes.push_back(nd);
                previous = Prev_Tok_Comma;
                break;

            case '(':
                // Expect left paren only after a comma or another left paren
                if (!(previous & LParen_Valid))
                    abortf("Not expecting left parenthesis at position %d in tree description", position_in_string);

                // Create new node above and to the left of the current node
                assert(!nd->left);
                curr_node_index++;
                if (curr_node_index == max_nodes)
                    abortf("malformed tree description (more than %d nodes specified)", max_nodes);
                nd->left = createNewNode(curr_node_index);
                nd->left->parent = nd;
                nd = nd->left;
                nodes.push_back(nd);
                previous = Prev_Tok_LParen;
                break;

            case '\'':
                // Encountered an apostrophe, which always indicates the start of a
                // node name (but note that node names do not have to be quoted)

                // Expect node name only after a left paren (child's name), a comma (sib's name)
                // or a right paren (parent's name)
                if (!(previous & Name_Valid))
                    abortf("Not expecting node name at position %d in tree description", position_in_string);

                // Get the rest of the name
                // nd->_name.clear();

                inside_quoted_name = true;
                node_name_position = position_in_string;

                break;

            default:
                // Get here if ch is not one of ();:,'

                // Expecting either an edge length or an unquoted node name
                if (previous == Prev_Tok_Colon)
                    {
                    // Edge length expected (e.g. "235", "0.12345", "1.7e-3")
                    inside_edge_length = true;
                    edge_length_position = position_in_string;
                    edge_length_str = ch;
                    }
                else
                    {
                    // Get the node name
                    // nd->_name = ch;

                    inside_unquoted_name = true;
                    node_name_position = position_in_string;
                    }

            }   // end of switch statement
        }   // loop over characters in newick string    


        if (inside_unquoted_name)
            abortf("Tree description ended before end of node name starting at position %d was found", node_name_position);
        if (inside_edge_length)
            abortf("Tree description ended before end of edge length starting at position %d was found", edge_length_position);
        if (inside_quoted_name)
            abortf("Expecting single quote to mark the end of node name at position %d in tree description", node_name_position);

        // root has to be highest index
        if (root->data != max_nodes-2)
            {
            node* tmp_node;
            for (auto n : nodes)
                {
                if (n->data == max_nodes-2)
                    {
                    tmp_node = n;
                    break;
                    }
                }
            tmp_node->data = nd->data;
            nd->data = max_nodes-2; 
            }

        // if (!rooted)
        //     {
        //     rerootAt(0);
        //     }
}

#endif // HAVE_NCL

#ifdef HAVE_PLL

char* pll_getNucleotideCharStates( int* states, int nsites )
{
    char *charStates = (char*) malloc(sizeof(char) * nsites); 
    for( int i=0; i<nsites; i++ )
    {
        switch(states[i]) {
            case 0:
                charStates[i] = 'A';
                break;
            case 1:
                charStates[i] = 'C';
                break;
            case 2:
                charStates[i] = 'G';
                break;
            case 3:
                charStates[i] = 'T';
                break;
            default:
                charStates[i] = '-';
        }
    }
    return charStates;
}


void pll_printTiming(double timingValue,
                double beagleTimingValue,
                 int timePrecision,
                 bool printSpeedup,
                 double cpuTimingValue,
                 int speedupPrecision,
                 bool printPercent,
                 double totalTime,
                 int percentPrecision) {
    std::cout << std::setprecision(timePrecision) << timingValue << " ms";
    if (printSpeedup) std::cout << " (" << std::setprecision(speedupPrecision) << beagleTimingValue/timingValue << "x BEAGLE)";
    if (printPercent) std::cout << " (" << std::setw(3+percentPrecision) << std::setfill('0') << std::setprecision(percentPrecision) << (double)(timingValue/totalTime)*100 << "%)";
    std::cout << "\n";
}

#endif // HAVE_PLL

void printTiming(double timingValue,
                 int timePrecision,
                 bool printSpeedup,
                 double cpuTimingValue,
                 int speedupPrecision,
                 bool printPercent,
                 double totalTime,
                 int percentPrecision) {
    std::cout << std::setprecision(timePrecision) << timingValue << " ms";
    if (printSpeedup) std::cout << " (" << std::setprecision(speedupPrecision) << cpuTimingValue/timingValue << "x CPU)";
    if (printPercent) std::cout << " (" << std::setw(3+percentPrecision) << std::setfill('0') << std::setprecision(percentPrecision) << (double)(timingValue/totalTime)*100 << "%)";
    std::cout << "\n";
}

double getTimeDiff(struct timeval t1,
                   struct timeval t2) {
    return ((double)(t2.tv_sec - t1.tv_sec)*1000.0 + (double)(t2.tv_usec-t1.tv_usec)/1000.0);
}

/* Given a binary tree, print its nodes according to the
"bottom-up" postorder traversal. */
void traversePostorder(node* currentNode, std::deque <node*> &S)
{
    if (currentNode == NULL)
        return;
 
    // first recur on left subtree
    traversePostorder(currentNode->left, S);
 
    // then recur on right subtree
    traversePostorder(currentNode->right, S);
 
    // now deal with the currentNode
    if (currentNode->left != NULL) {
        S.push_front(currentNode);
    }
}
 

/* Given a binary tree, print its nodes in reverse level order */
void reverseLevelOrder(node* root, std::deque <node*> &S)
{
    std::queue <node*> Q;
    Q.push(root);
 
    // Do something like normal level order traversal order. Following are the
    // differences with normal level order traversal
    // 1) Instead of printing a node, we push the node to stack
    // 2) Right subtree is visited before left subtree
    while (Q.empty() == false)
    {
        /* Dequeue node and make it root */
        root = Q.front();
        Q.pop();

        if (root->left!=NULL) {
            S.push_back(root);
        }
 
        /* Enqueue right child */
        if (root->right)
            Q.push(root->right); // NOTE: RIGHT CHILD IS ENQUEUED BEFORE LEFT
 
        /* Enqueue left child */
        if (root->left)
            Q.push(root->left);
    }
 
}


/* Given a binary tree, count number of parallel launches */
int countLaunches(node* root, bool postorderTraversal)
{
    std::deque <node *> S;
    if (postorderTraversal == true) {
        traversePostorder(root, S);
    } else {
        reverseLevelOrder(root, S);
    }

    int opCount = S.size();

    int launchCount = 0;
    std::vector<int> gridStartOp(opCount);
    std::vector<int> operationsTmp(opCount);
    int parentMinIndex = 0;

    for(int op=0; op<opCount; op++){
        node* parent = S.back();
        S.pop_back();
        int parentIndex = parent->data;
        int child1Index = parent->left->data;
        int child2Index = parent->right->data;

        operationsTmp[op] = parentIndex;
        
        // printf("op %02d dest %02d c1 %02d c2 %02d\n",
        //        op, parentIndex, child1Index, child2Index);

        bool newLaunch = false;

        if (op == 0) {
            newLaunch = true;
        } else if (child1Index >= parentMinIndex || child2Index >= parentMinIndex) {
            for (int i=gridStartOp[launchCount-1]; i < op; i++) {
                int previousParentIndex = operationsTmp[i];
                if (child1Index == previousParentIndex || child2Index == previousParentIndex) {
                    newLaunch = true;
                    break;
                }
            }
        }

       if (newLaunch) {
            gridStartOp[launchCount] = op;
            parentMinIndex = parentIndex;

            launchCount++;
        } 

        if (parentIndex < parentMinIndex)
            parentMinIndex = parentIndex;
    }

    return launchCount;
}

void addChildren(node* newNode, node* originalNode, std::vector<node*> newNodes)
{
    if (originalNode->left != NULL) {
        newNode->left = createNewNode(originalNode->left->data);
        newNode->left->parent = newNode;
        newNodes.push_back(newNode->left);
        
        addChildren(newNode->left, originalNode->left, newNodes);

        newNode->right = createNewNode(originalNode->right->data);
        newNode->right->parent = newNode;
        newNodes.push_back(newNode->right);

        addChildren(newNode->right, originalNode->right, newNodes);
    }
}


void addParentChildren(node* newNode, node* originalNode, std::vector<node*> newNodes)
{

    if (originalNode->parent != NULL) {

        if (originalNode->left->data == newNode->parent->data) {
            newNode->left = createNewNode(originalNode->parent->data);
            newNode->left->parent = newNode;
            newNodes.push_back(newNode->left);

            addParentChildren(newNode->left, originalNode->parent, newNodes);

            newNode->right = createNewNode(originalNode->right->data);
            newNode->right->parent = newNode;
            newNodes.push_back(newNode->right);

            addChildren(newNode->right, originalNode->right, newNodes);
        } else {
            newNode->right = createNewNode(originalNode->parent->data);
            newNode->right->parent = newNode;
            newNodes.push_back(newNode->right);

            addParentChildren(newNode->right, originalNode->parent, newNodes);

            newNode->left = createNewNode(originalNode->left->data);
            newNode->left->parent = newNode;
            newNodes.push_back(newNode->left);

            addChildren(newNode->left, originalNode->left, newNodes);
        }

    } else { // original is root node

        if (newNode->parent->data == originalNode->left->data) {
            newNode->data = originalNode->right->data;
            addChildren(newNode, originalNode->right, newNodes);
        } else {
            newNode->data = originalNode->left->data;
            addChildren(newNode, originalNode->left, newNodes);
        }
    }
}

node* reroot(node* rerootNode, node* root, std::vector<node*> newNodes)
{
    struct node* newRoot = createNewNode(rerootNode->data);
    newNodes.push_back(newRoot);

        if (rerootNode->parent->left == rerootNode) {

            newRoot->left = createNewNode(rerootNode->data);
            newRoot->left->parent = newRoot;
            newNodes.push_back(newRoot->left);

            addChildren(newRoot->left, rerootNode, newNodes);

            newRoot->right = createNewNode(rerootNode->parent->data);
            newRoot->right->parent = newRoot;
            newNodes.push_back(newRoot->right);

            addParentChildren(newRoot->right, rerootNode->parent, newNodes);

        } else {

            newRoot->right = createNewNode(rerootNode->data);
            newRoot->right->parent = newRoot;
            newNodes.push_back(newRoot->right);

            addChildren(newRoot->right, rerootNode, newNodes);

            newRoot->left = createNewNode(rerootNode->parent->data);
            newRoot->left->parent = newRoot;
            newNodes.push_back(newRoot->left);

            addParentChildren(newRoot->left, rerootNode->parent, newNodes);

        }

        newRoot->data = root->data;

        return newRoot;
}

void generateNewTree(int ntaxa,
                    bool rerootTrees,
                    bool pectinate,
                    bool postorderTraversal,
                    bool dynamicScaling,
                    int edgeCount,
                    int internalCount,
                    int unpartOpsCount,
                    int partitionCount,
                    int beagleOpCount,
#ifdef HAVE_PLL
                    bool pllTest,
                    pll_operation_t* pll_operations,
#endif
#ifdef HAVE_NCL
                    char* treenewick,
#endif 
                    int* operations)
{
    std::vector <node*> nodes;
    node* root = NULL;


    bool useNewickTree = false;

#ifdef HAVE_NCL
    useNewickTree = treenewick;
#endif 

    if (!useNewickTree)
    {
        nodes.push_back(createNewNode(0));
        int tipsAdded = 1;
        node* newParent;
        while (tipsAdded < ntaxa) {
            int sibling;
            if (pectinate)
                sibling = nodes.size()-1;
            else
                sibling = gt_rand() % nodes.size();
            node* newTip = createNewNode(tipsAdded);
            newParent = createNewNode(ntaxa + tipsAdded - 1);
            nodes.push_back(newTip);
            nodes.push_back(newParent);
            tipsAdded++;            
            newParent->left  = nodes[sibling];
            newParent->right = newTip;            
            if (nodes[sibling]->parent != NULL) {
                newParent->parent = nodes[sibling]->parent;
                if (nodes[sibling]->parent->left == nodes[sibling]) {
                    nodes[sibling]->parent->left = newParent;
                } else {
                    nodes[sibling]->parent->right = newParent;
                }
            }
            nodes[sibling]->parent = newParent;
            newTip->parent         = newParent;
        }
        root = nodes[0];
        while(root->parent != NULL) {
            root = root->parent;
        }
        int rootIndex = newParent->data;
        newParent->data = root->data;
        root->data = rootIndex;
    } else {
        root = createNewNode(0);
#ifdef HAVE_NCL
        ncl_generateTreeFromNewick(treenewick, ntaxa, nodes, root);
#endif 
    }

    if (rerootTrees) {
        int bestRerootNode = -1;
        int bestLaunchCount = countLaunches(root, postorderTraversal);

        // printf("\nroot node   = %d\tparallel launches = %d\n", root->data, bestLaunchCount);


        std::vector<node*> newNodes;

        for(int i = 0; i < nodes.size(); i++) {

            // printf("reroot node = %02d\t", nodes[i]->data);

            node* rerootNode = nodes[i];

            if (rerootNode->parent != NULL && rerootNode->parent != root) {
                
                node* newRoot = reroot(rerootNode, root, newNodes);

                int launchCount = countLaunches(newRoot, postorderTraversal);

                newNodes.clear();

                // printf("parallel launches = %d\n", launchCount);

                if (launchCount < bestLaunchCount) {
                    bestLaunchCount = launchCount;
                    bestRerootNode = i;
                }

            }
            // else {printf("doesn't change tree\n");}

        }

        if (bestRerootNode != -1) {
            // printf("\nbestLaunchCount = %d, node index = %d\n\n", bestLaunchCount, bestRerootNode);
            node* rerootNode = nodes[bestRerootNode];
            node* oldRoot = root;
            root = reroot(rerootNode, oldRoot, newNodes);
        }

    } 

    std::deque <node *> S;
    if (postorderTraversal == true) {
        traversePostorder(root, S);
    } else {
        reverseLevelOrder(root, S);
    }

    // while (S.empty() == false) {
    //     node* tmpNode = S.back();
    //     std::cout << tmpNode->data << " ";
    //     S.pop_back();
    // }
    // std::cout << std::endl;
    // reverseLevelOrder(root, S);

    // struct node *root = createNewNode(4);
    // root->left        = createNewNode(0);
    // root->right       = createNewNode(6);
    // root->right->left  = createNewNode(5);
    // root->right->right = createNewNode(3);
    // root->right->left->left  = createNewNode(1);
    // root->right->left->right = createNewNode(2);
    // std::deque <node *> S;
    // reverseLevelOrder(root, S);

    // printf("launch count = %03d", countLaunches(root));

    for(int op=0; op<unpartOpsCount; op++){
        node* parent = S.back();
        S.pop_back();
        int parentIndex = parent->data;
        int child1Index = parent->left->data;
        int child2Index = parent->right->data;

        for (int j=0; j<partitionCount; j++) {
            int opJ = partitionCount*op + j;
            operations[opJ*beagleOpCount+0] = parentIndex;
            operations[opJ*beagleOpCount+1] = (dynamicScaling ? parentIndex : BEAGLE_OP_NONE);
            operations[opJ*beagleOpCount+2] = (dynamicScaling ? parentIndex : BEAGLE_OP_NONE);
            operations[opJ*beagleOpCount+3] = child1Index;
            operations[opJ*beagleOpCount+4] = child1Index + j*edgeCount;;
            operations[opJ*beagleOpCount+5] = child2Index;
            operations[opJ*beagleOpCount+6] = child2Index + j*edgeCount;
            if (partitionCount > 1) {
                operations[opJ*beagleOpCount+7] = j;
                operations[opJ*beagleOpCount+8] = (dynamicScaling ? internalCount : BEAGLE_OP_NONE);
            }

    #ifdef HAVE_PLL
            if (pllTest) {
                pll_operations[op].parent_clv_index    = parentIndex;
                pll_operations[op].child1_clv_index    = child1Index;
                pll_operations[op].child2_clv_index    = child2Index;
                pll_operations[op].child1_matrix_index = child1Index + j*edgeCount;
                pll_operations[op].child2_matrix_index = child2Index + j*edgeCount;
                pll_operations[op].parent_scaler_index = PLL_SCALE_BUFFER_NONE;
                pll_operations[op].child1_scaler_index = PLL_SCALE_BUFFER_NONE;
                pll_operations[op].child2_scaler_index = PLL_SCALE_BUFFER_NONE;
            }
    #endif // HAVE_PLL

        // printf("op %02d part %02d dest %02d c1 %02d c2 %02d\n",
               // opJ, j, parentIndex, child1Index, child2Index);
        }
        // printf("\n");
    }   

}

void setNewCategoryRates(int partitionCount,
                         int rateCategoryCount,
                         int instanceCount,
                         std::vector<int> instances,
#ifdef HAVE_PLL
                         bool pllTest,
                         bool pllOnly,
                         pll_partition_t* pll_partition,
#endif
                         double* rates)
{
    for (int i = 0; i < rateCategoryCount; i++) {
        rates[i] = gt_rand() / (double) GT_RAND_MAX;
    }
    
    if (partitionCount > 1) {
        for (int i=0; i < partitionCount; i++) {
#ifdef HAVE_PLL
            if (!pllOnly) {
#endif
            beagleSetCategoryRatesWithIndex(instances[0], i, &rates[0]);
#ifdef HAVE_PLL
            } //if (!pllOnly) {
#endif
        }
    } else {
        for(int inst=0; inst<instanceCount; inst++) {
#ifdef HAVE_PLL
            if (!pllOnly) {
#endif
            beagleSetCategoryRates(instances[inst], &rates[0]);
#ifdef HAVE_PLL
            } //if (!pllOnly) {
#endif
        }
#ifdef HAVE_PLL
        if (pllTest) {
            pll_set_category_rates(pll_partition, rates);
        }
#endif // HAVE_PLL
    }
}

void setNewPatternWeights(int nsites,
                          int instanceCount,
                          std::vector<int> instances,
                          std::vector<int> instanceSitesCount,
#ifdef HAVE_PLL
                          bool pllTest,
                          bool pllOnly,
                          pll_partition_t* pll_partition,
#endif
                          double* patternWeights)
{
    for (int i = 0; i < nsites; i++) {
        patternWeights[i] =  gt_rand()%10;
    }    

    size_t instanceOffset = 0;
    for(int inst=0; inst<instanceCount; inst++) {
#ifdef HAVE_PLL
        if (!pllOnly) {
#endif
        beagleSetPatternWeights(instances[inst], patternWeights + instanceOffset);
        instanceOffset += instanceSitesCount[inst];
#ifdef HAVE_PLL
        } //if (!pllOnly) {
#endif
    }

#ifdef HAVE_PLL
    if (pllTest) {
        unsigned int* pll_patternWeights = (unsigned int*) malloc(sizeof(unsigned int) * nsites);
        for (int i = 0; i < nsites; i++) {
            pll_patternWeights[i] = (unsigned int) patternWeights[i];
        }    
        pll_set_pattern_weights(pll_partition, pll_patternWeights);
        free(pll_patternWeights);
    }
#endif // HAVE_PLL
}

void setNewCategoryWeights(int eigenCount,
                           int rateCategoryCount,
                           int instanceCount,
                           std::vector<int> instances,
#ifdef HAVE_PLL
                           bool pllTest,
                           bool pllOnly,
                           pll_partition_t* pll_partition,
#endif
                           double* weights)

{
    for (int eigenIndex=0; eigenIndex < eigenCount; eigenIndex++) {
        for (int i = 0; i < rateCategoryCount; i++) {
            weights[i] = gt_rand() / (double) GT_RAND_MAX;
        } 

        for(int inst=0; inst<instanceCount; inst++) {
#ifdef HAVE_PLL
            if (!pllOnly) {
#endif
            beagleSetCategoryWeights(instances[inst], eigenIndex, &weights[0]);
#ifdef HAVE_PLL
            } //if (!pllOnly) {
#endif
        }

#ifdef HAVE_PLL
        if (pllTest) {
            pll_set_category_weights(pll_partition, &weights[0]);
        }
#endif // HAVE_PLL

    }
}

void setNewEigenModels(int modelCount,
                       int stateCount,
                       double* freqs,
                       bool eigencomplex,
                       bool ievectrans,
                       bool setmatrix,
                       int eigenCount,
                       int instanceCount,
#ifdef HAVE_PLL
                       bool pllTest,
                       bool pllOnly,
                       pll_partition_t* pll_partition,
#endif
                       std::vector<int> instances)
{
    double* eval;
    if (!eigencomplex)
        eval = (double*)malloc(sizeof(double)*stateCount);
    else
        eval = (double*)malloc(sizeof(double)*stateCount*2);
    double* evec = (double*)malloc(sizeof(double)*stateCount*stateCount);
    double* ivec = (double*)malloc(sizeof(double)*stateCount*stateCount);
    
    for (int eigenIndex=0; eigenIndex < modelCount; eigenIndex++) {
        if (!eigencomplex && ((stateCount & (stateCount-1)) == 0)) {
            
            for (int i=0; i<stateCount; i++) {
                freqs[i] = 1.0 / stateCount;
            }

            // an eigen decomposition for the general state-space JC69 model
            // If stateCount = 2^n is a power-of-two, then Sylvester matrix H_n describes
            // the eigendecomposition of the infinitesimal rate matrix
             
            double* Hn = evec;
            Hn[0*stateCount+0] = 1.0; Hn[0*stateCount+1] =  1.0; 
            Hn[1*stateCount+0] = 1.0; Hn[1*stateCount+1] = -1.0; // H_1
         
            for (int k=2; k < stateCount; k <<= 1) {
                // H_n = H_1 (Kronecker product) H_{n-1}
                for (int i=0; i<k; i++) {
                    for (int j=i; j<k; j++) {
                        double Hijold = Hn[i*stateCount + j];
                        Hn[i    *stateCount + j + k] =  Hijold;
                        Hn[(i+k)*stateCount + j    ] =  Hijold;
                        Hn[(i+k)*stateCount + j + k] = -Hijold;
                        
                        Hn[j    *stateCount + i + k] = Hn[i    *stateCount + j + k];
                        Hn[(j+k)*stateCount + i    ] = Hn[(i+k)*stateCount + j    ];
                        Hn[(j+k)*stateCount + i + k] = Hn[(i+k)*stateCount + j + k];                                
                    }
                }        
            }
            
            // Since evec is Hadamard, ivec = (evec)^t / stateCount;    
            for (int i=0; i<stateCount; i++) {
                for (int j=i; j<stateCount; j++) {
                    ivec[i*stateCount+j] = evec[j*stateCount+i] / stateCount;
                    ivec[j*stateCount+i] = ivec[i*stateCount+j]; // Symmetric
                }
            }
           
            eval[0] = 0.0;
            for (int i=1; i<stateCount; i++) {
                eval[i] = -stateCount / (stateCount - 1.0);
            }
       
        } else if (!eigencomplex) {
            for (int i=0; i<stateCount; i++) {
                freqs[i] = gt_rand() / (double) GT_RAND_MAX;
            }
        
            double** qmat=New2DArray<double>(stateCount, stateCount);    
            double* relNucRates = new double[(stateCount * stateCount - stateCount) / 2];
            
            int rnum=0;
            for(int i=0;i<stateCount;i++){
                for(int j=i+1;j<stateCount;j++){
                    relNucRates[rnum] = gt_rand() / (double) GT_RAND_MAX;
                    qmat[i][j]=relNucRates[rnum] * freqs[j];
                    qmat[j][i]=relNucRates[rnum] * freqs[i];
                    rnum++;
                }
            }

            //set diags to sum rows to 0
            double sum;
            for(int x=0;x<stateCount;x++){
                sum=0.0;
                for(int y=0;y<stateCount;y++){
                    if(x!=y) sum+=qmat[x][y];
                        }
                qmat[x][x]=-sum;
            } 
            
            double* eigvalsimag=new double[stateCount];
            double** eigvecs=New2DArray<double>(stateCount, stateCount);//eigenvecs
            double** teigvecs=New2DArray<double>(stateCount, stateCount);//temp eigenvecs
            double** inveigvecs=New2DArray<double>(stateCount, stateCount);//inv eigenvecs    
            int* iwork=new int[stateCount];
            double* work=new double[stateCount];
            
            EigenRealGeneral(stateCount, qmat, eval, eigvalsimag, eigvecs, iwork, work);
            memcpy(*teigvecs, *eigvecs, stateCount*stateCount*sizeof(double));
            InvertMatrix(teigvecs, stateCount, work, iwork, inveigvecs);
            
            for(int x=0;x<stateCount;x++){
                for(int y=0;y<stateCount;y++){
                    evec[x * stateCount + y] = eigvecs[x][y];
                    if (ievectrans)
                        ivec[x * stateCount + y] = inveigvecs[y][x];
                    else
                        ivec[x * stateCount + y] = inveigvecs[x][y];
                }
            } 
            
            Delete2DArray(qmat);
            delete[] relNucRates;
            
            delete[] eigvalsimag;
            Delete2DArray(eigvecs);
            Delete2DArray(teigvecs);
            Delete2DArray(inveigvecs);
            delete[] iwork;
            delete[] work;
        } else if (eigencomplex && stateCount==4 && eigenCount==1) {
            // create base frequency array
            double temp_freqs[4] = { 0.25, 0.25, 0.25, 0.25 };
            
            // an eigen decomposition for the 4-state 1-step circulant infinitesimal generator
            double temp_evec[4 * 4] = {
                -0.5,  0.6906786606674509,   0.15153543380548623, 0.5,
                0.5, -0.15153543380548576,  0.6906786606674498,  0.5,
                -0.5, -0.6906786606674498,  -0.15153543380548617, 0.5,
                0.5,  0.15153543380548554, -0.6906786606674503,  0.5
            };
            
            double temp_ivec[4 * 4] = {
                -0.5,  0.5, -0.5,  0.5,
                0.6906786606674505, -0.15153543380548617, -0.6906786606674507,   0.15153543380548645,
                0.15153543380548568, 0.6906786606674509,  -0.15153543380548584, -0.6906786606674509,
                0.5,  0.5,  0.5,  0.5
            };
            
            double temp_eval[8] = { -2.0, -1.0, -1.0, 0, 0, 1, -1, 0 };
            
            for(int x=0;x<stateCount;x++){
                freqs[x] = temp_freqs[x];
                eval[x] = temp_eval[x];
                eval[x+stateCount] = temp_eval[x+stateCount];
                for(int y=0;y<stateCount;y++){
                    evec[x * stateCount + y] = temp_evec[x * stateCount + y];
                    if (ievectrans)
                        ivec[x * stateCount + y] = temp_ivec[x + y * stateCount];
                    else
                        ivec[x * stateCount + y] = temp_ivec[x * stateCount + y];
                }
            } 
        } else {
            abort("should not be here");
        }

        for(int inst=0; inst<instanceCount; inst++) {
#ifdef HAVE_PLL
           if (!pllOnly) {
#endif
            beagleSetStateFrequencies(instances[inst], eigenIndex, &freqs[0]);
#ifdef HAVE_PLL
            } //if (!pllOnly) {
#endif
        }

#ifdef HAVE_PLL
        if (pllTest) {
            pll_set_frequencies(pll_partition, 0, &freqs[0]);
        }
#endif // HAVE_PLL
        if (!setmatrix) {
            // set the Eigen decomposition
            for(int inst=0; inst<instanceCount; inst++) {
#ifdef HAVE_PLL
               if (!pllOnly) {
#endif
                beagleSetEigenDecomposition(instances[inst], eigenIndex, &evec[0], &ivec[0], &eval[0]);
#ifdef HAVE_PLL
                } //if (!pllOnly) {
#endif
            }
#ifdef HAVE_PLL
            if (pllTest) {
                double pll_subst_params[6] = {1,1,1,1,1,1};
                pll_set_subst_params(pll_partition, 0, pll_subst_params);
            }
#endif // HAVE_PLL
        }
    }
    
    free(eval);
    free(evec);
    free(ivec);
}

void printFlags(BeagleFlagsType inFlags) {
    if (inFlags & BEAGLE_FLAG_PRECISION_SINGLE   ) fprintf(stdout, " PRECISION_SINGLE"   );
    if (inFlags & BEAGLE_FLAG_PRECISION_DOUBLE   ) fprintf(stdout, " PRECISION_DOUBLE"   );
    if (inFlags & BEAGLE_FLAG_COMPUTATION_SYNCH  ) fprintf(stdout, " COMPUTATION_SYNCH"  );
    if (inFlags & BEAGLE_FLAG_COMPUTATION_ASYNCH ) fprintf(stdout, " COMPUTATION_ASYNCH" );
    if (inFlags & BEAGLE_FLAG_EIGEN_REAL         ) fprintf(stdout, " EIGEN_REAL"         );
    if (inFlags & BEAGLE_FLAG_EIGEN_COMPLEX      ) fprintf(stdout, " EIGEN_COMPLEX"      );
    if (inFlags & BEAGLE_FLAG_SCALING_MANUAL     ) fprintf(stdout, " SCALING_MANUAL"     );
    if (inFlags & BEAGLE_FLAG_SCALING_AUTO       ) fprintf(stdout, " SCALING_AUTO"       );
    if (inFlags & BEAGLE_FLAG_SCALING_ALWAYS     ) fprintf(stdout, " SCALING_ALWAYS"     );
    if (inFlags & BEAGLE_FLAG_SCALING_DYNAMIC    ) fprintf(stdout, " SCALING_DYNAMIC"    );
    if (inFlags & BEAGLE_FLAG_SCALERS_RAW        ) fprintf(stdout, " SCALERS_RAW"        );
    if (inFlags & BEAGLE_FLAG_SCALERS_LOG        ) fprintf(stdout, " SCALERS_LOG"        );
    if (inFlags & BEAGLE_FLAG_INVEVEC_STANDARD   ) fprintf(stdout, " INVEVEC_STANDARD"   );
    if (inFlags & BEAGLE_FLAG_INVEVEC_TRANSPOSED ) fprintf(stdout, " INVEVEC_TRANSPOSED" );
    if (inFlags & BEAGLE_FLAG_VECTOR_SSE         ) fprintf(stdout, " VECTOR_SSE"         );
    if (inFlags & BEAGLE_FLAG_VECTOR_AVX         ) fprintf(stdout, " VECTOR_AVX"         );
    if (inFlags & BEAGLE_FLAG_VECTOR_NONE        ) fprintf(stdout, " VECTOR_NONE"        );
    if (inFlags & BEAGLE_FLAG_THREADING_CPP      ) fprintf(stdout, " THREADING_CPP"      );
    if (inFlags & BEAGLE_FLAG_THREADING_OPENMP   ) fprintf(stdout, " THREADING_OPENMP"   );
    if (inFlags & BEAGLE_FLAG_THREADING_NONE     ) fprintf(stdout, " THREADING_NONE"     );
    if (inFlags & BEAGLE_FLAG_PROCESSOR_CPU      ) fprintf(stdout, " PROCESSOR_CPU"      );
    if (inFlags & BEAGLE_FLAG_PROCESSOR_GPU      ) fprintf(stdout, " PROCESSOR_GPU"      );
    if (inFlags & BEAGLE_FLAG_PROCESSOR_FPGA     ) fprintf(stdout, " PROCESSOR_FPGA"     );
    if (inFlags & BEAGLE_FLAG_PROCESSOR_CELL     ) fprintf(stdout, " PROCESSOR_CELL"     );
    if (inFlags & BEAGLE_FLAG_PROCESSOR_PHI      ) fprintf(stdout, " PROCESSOR_PHI"      );
    if (inFlags & BEAGLE_FLAG_PROCESSOR_OTHER    ) fprintf(stdout, " PROCESSOR_OTHER"    );
    if (inFlags & BEAGLE_FLAG_FRAMEWORK_CUDA     ) fprintf(stdout, " FRAMEWORK_CUDA"     );
    if (inFlags & BEAGLE_FLAG_FRAMEWORK_OPENCL   ) fprintf(stdout, " FRAMEWORK_OPENCL"   );
    if (inFlags & BEAGLE_FLAG_FRAMEWORK_CPU      ) fprintf(stdout, " FRAMEWORK_CPU"      );
    if (inFlags & BEAGLE_FLAG_PARALLELOPS_STREAMS) fprintf(stdout, " PARALLELOPS_STREAMS");
    if (inFlags & BEAGLE_FLAG_PARALLELOPS_GRID   ) fprintf(stdout, " PARALLELOPS_GRID"   );
}


void threadWaiting(threadData* tData)
{
    std::unique_lock<std::mutex> l(tData->m, std::defer_lock);
    while (true)
    {
        l.lock();

        // Wait until the queue won't be empty or stop is signaled
        tData->cv.wait(l, [tData] () {
            return (tData->stop || !tData->jobs.empty()); 
            });

        // Stop was signaled, let's exit the thread
        if (tData->stop) { return; }

        // Pop one task from the queue...
        std::packaged_task<void()> j = std::move(tData->jobs.front());
        tData->jobs.pop();

        l.unlock();

        // Execute the task!
        j();
    }
}

void runBeagle(int resource, 
               int stateCount, 
               int ntaxa, 
               int nsites, 
               bool manualScaling, 
               bool autoScaling,
               bool dynamicScaling,
               int rateCategoryCount,
               int nreps,
               bool fullTiming,
               bool requireDoublePrecision,
               bool disableVector,
               bool enableThreads,
               int compactTipCount,
               int randomSeed,
               int rescaleFrequency,
               bool unrooted,
               bool calcderivs,
               bool logscalers,
               int eigenCount,
               bool eigencomplex,
               bool ievectrans,
               bool setmatrix,
               bool opencl,
               int partitionCount,
               bool sitelikes,
               bool newDataPerRep,
               bool randomTree,
               bool rerootTrees,
               bool pectinate,
               bool benchmarklist,
               bool pllTest,
               bool pllSiteRepeats,
               bool pllOnly,
               bool multiRsrc,
               bool postorderTraversal,
               bool newTreePerRep,
               bool newParametersPerRep,
               int  threadCount,
               int* resourceList,
               int  resourceCount,
               bool alignmentFromFile,
               char* treenewick,
               bool clientThreadingEnabled)
{

    int instanceCount = 1;

    std::vector<int> instanceSitesCount;

    if (multiRsrc == true) {
        instanceCount = resourceCount;
    }

    for(int inst=0; inst<instanceCount; inst++) {
        instanceSitesCount.push_back(nsites/instanceCount);
    }

    if (instanceCount > 1) {
        int remainder = nsites % instanceCount;
        int currentInstance = 0;
        while (remainder != 0) {
            instanceSitesCount[currentInstance++]++;
            remainder--;
        }
    }
    
    int edgeCount = ntaxa*2-2;
    int internalCount = ntaxa-1;
    int partialCount = ((ntaxa+internalCount)-compactTipCount)*eigenCount;
    int scaleCount = ((manualScaling || dynamicScaling) ? ntaxa : 0);

    int modelCount = eigenCount * partitionCount;
    
    BeagleInstanceDetails instDetails;
    

    if (benchmarklist) {
        // print version and citation info
        fprintf(stdout, "BEAGLE version %s\n", beagleGetVersion());
        fprintf(stdout, "%s\n", beagleGetCitation());

        BeagleFlagsType benchmarkFlags = BEAGLE_BENCHFLAG_SCALING_NONE;

        if (manualScaling) {
            if (rescaleFrequency > 1)
                benchmarkFlags = BEAGLE_BENCHFLAG_SCALING_DYNAMIC;
            else
                benchmarkFlags = BEAGLE_BENCHFLAG_SCALING_ALWAYS;
        }

        BeagleFlagsType preferenceFlags = (enableThreads ? BEAGLE_FLAG_THREADING_CPP : 0);
        BeagleFlagsType requirementFlags =
        (requireDoublePrecision ? BEAGLE_FLAG_PRECISION_DOUBLE : BEAGLE_FLAG_PRECISION_SINGLE) |
	  (disableVector ? BEAGLE_FLAG_VECTOR_NONE : 0);

        // print resource list
        BeagleBenchmarkedResourceList* rBList;
        rBList = beagleGetBenchmarkedResourceList(
                    ntaxa,
                    compactTipCount,
                    stateCount,
                    nsites,
                    rateCategoryCount,
                    resourceList,
                    resourceCount,
                    preferenceFlags,
                    requirementFlags,
                    eigenCount,
                    partitionCount,
                    calcderivs,
                    benchmarkFlags);

        fprintf(stdout, "Resource benchmarks:\n");
        for (int i = 0; i < rBList->length; i++) {
            fprintf(stdout, "\tResource %i:\n\t\tName : %s\n", i, rBList->list[i].name);
            fprintf(stdout, "\t\tDesc : %s\n", rBList->list[i].description);
            fprintf(stdout, "\t\tSupport Flags:");
            printFlags(rBList->list[i].supportFlags);
            fprintf(stdout, "\n");
            fprintf(stdout, "\t\tRequired Flags:");
            printFlags(rBList->list[i].requiredFlags);
            fprintf(stdout, "\n");
            fprintf(stdout, "\t\tBenchmark Results:\n");
            fprintf(stdout, "\t\t\tNmbr : %d\n", rBList->list[i].number);
            fprintf(stdout, "\t\t\tImpl : %s\n", rBList->list[i].implName);
            fprintf(stdout, "\t\t\tFlags:");
            printFlags(rBList->list[i].benchedFlags);
            fprintf(stdout, "\n");
            fprintf(stdout, "\t\t\tPerf : %.4f ms (%.2fx CPU)\n", rBList->list[i].benchmarkResult, rBList->list[i].performanceRatio);
        }
        fprintf(stdout, "\n");
        std::exit(0);
    }

#ifdef HAVE_PLL
    pll_partition_t * pll_partition;
    pll_operation_t * pll_operations;
    unsigned int * pll_params_indices;

    if (pllTest) {
        int pll_num_params = 4;
        pll_params_indices = (unsigned int *) malloc(pll_num_params * sizeof(unsigned int));
        for (int i = 0; i < pll_num_params; i++) {
            pll_params_indices[i] = 0;
        }

        long pll_attribs = PLL_ATTRIB_ARCH_AVX2;
        if (pllSiteRepeats) {
            pll_attribs |= PLL_ATTRIB_SITE_REPEATS;
        } else if (compactTipCount == ntaxa) {
            pll_attribs |= PLL_ATTRIB_PATTERN_TIP;
        }

        pll_partition = pll_partition_create(ntaxa,
                                       partialCount,           /* clv buffers */
                                       stateCount, /* number of states */
                                       nsites,     /* sequence length */
                                       modelCount,           /* different rate parameters */
                                       edgeCount*modelCount,  /* probability matrices */
                                       rateCategoryCount, /* gamma categories */
                                       scaleCount*eigenCount,           /* scale buffers */
                                       pll_attribs
                                       );          /* attributes */
    }
#endif // HAVE_PLL

    std::vector<int> instances;
#ifdef HAVE_PLL
    if (!pllOnly) {
#endif
    for(int inst=0; inst<instanceCount; inst++) {

        int instanceResource = resource;
        
        if (multiRsrc) {
            instanceResource = resourceList[inst];
        }

        // create an instance of the BEAGLE library
        int instance = beagleCreateInstance(
                    ntaxa,            /**< Number of tip data elements (input) */
                    partialCount, /**< Number of partials buffers to create (input) */
                    compactTipCount,    /**< Number of compact state representation buffers to create (input) */
                    stateCount,       /**< Number of states in the continuous-time Markov chain (input) */
                    instanceSitesCount[inst],           /**< Number of site patterns to be handled by the instance (input) */
                    modelCount,               /**< Number of rate matrix eigen-decomposition buffers to allocate (input) */
                    (calcderivs ? (3*edgeCount*modelCount) : edgeCount*modelCount),/**< Number of rate matrix buffers (input) */
                    rateCategoryCount,/**< Number of rate categories */
                    scaleCount*eigenCount,          /**< scaling buffers */
                    &instanceResource,        /**< List of potential resource on which this instance is allowed (input, NULL implies no restriction */
                    1,                /**< Length of resourceList list (input) */
                    (enableThreads ? BEAGLE_FLAG_THREADING_CPP : 0) |
                    ((multiRsrc && !clientThreadingEnabled) ? BEAGLE_FLAG_COMPUTATION_ASYNCH : 0) |
		    (multiRsrc ? BEAGLE_FLAG_PARALLELOPS_STREAMS : 0),         /**< Bit-flags indicating preferred implementation charactertistics, see BeagleFlags (input) */
                    (disableVector ? BEAGLE_FLAG_VECTOR_NONE : 0) |
                    (opencl ? BEAGLE_FLAG_FRAMEWORK_OPENCL : 0) |
                    (ievectrans ? BEAGLE_FLAG_INVEVEC_TRANSPOSED : BEAGLE_FLAG_INVEVEC_STANDARD) |
                    (logscalers ? BEAGLE_FLAG_SCALERS_LOG : BEAGLE_FLAG_SCALERS_RAW) |
                    (eigencomplex ? BEAGLE_FLAG_EIGEN_COMPLEX : BEAGLE_FLAG_EIGEN_REAL) |
                    (dynamicScaling ? BEAGLE_FLAG_SCALING_DYNAMIC : 0) |
                    (autoScaling ? BEAGLE_FLAG_SCALING_AUTO : 0) |
                    (requireDoublePrecision ? BEAGLE_FLAG_PRECISION_DOUBLE : BEAGLE_FLAG_PRECISION_SINGLE) ,   /**< Bit-flags indicating required implementation characteristics, see BeagleFlags (input) */
                    &instDetails);

        if (instance < 0) {
            fprintf(stderr, "Failed to obtain BEAGLE instance\n\n");
            return;
        } else {
            instances.push_back(instance);

            int rNumber = instDetails.resourceNumber;
            fprintf(stdout, "Using resource %i:\n", rNumber);
            fprintf(stdout, "\tRsrc Name : %s\n",instDetails.resourceName);
            fprintf(stdout, "\tImpl Name : %s\n", instDetails.implName);    
            fprintf(stdout, "\tFlags:");
            printFlags(instDetails.flags);
            fprintf(stdout, "\n\n");

            if (inst+1 < instanceCount) {
                fprintf(stdout, "and\n\n");
            }

            if (threadCount > 1) {
                beagleSetCPUThreadCount(instance, threadCount);
            }

        }
    }
#ifdef HAVE_PLL
    } //if (!pllOnly)
#endif
        
    if (!(instDetails.flags & BEAGLE_FLAG_SCALING_AUTO))
        autoScaling = false;
    
    // set the sequences for each tip using partial likelihood arrays
    gt_srand(randomSeed);   // fix the random seed...
    for(int i=0; i<ntaxa; i++)
    {
        if (compactTipCount == 0 || (i >= (compactTipCount-1) && i != (ntaxa-1))) {
            double* tmpPartials = getRandomTipPartials(nsites, stateCount);
            size_t instanceOffset = 0;
            for(int inst=0; inst<instanceCount; inst++) {
#ifdef HAVE_PLL
                if (!pllOnly) {
#endif
                beagleSetTipPartials(instances[inst], i, tmpPartials + instanceOffset);
#ifdef HAVE_PLL
                } //if (!pllOnly)
#endif
                instanceOffset += instanceSitesCount[inst]*stateCount;
            }
#ifdef HAVE_PLL
            if (pllTest) {
                pll_set_tip_clv(pll_partition, i, tmpPartials, 0);
            }
#endif // HAVE_PLL
            free(tmpPartials);
        } else {
            int* tmpStates;
            if (!alignmentFromFile) {
                tmpStates = getRandomTipStates(nsites, stateCount);
            }
#ifdef HAVE_NCL
            else {
                tmpStates = ncl_getAlignmentTipStates(nsites, i);
            }
#endif // HAVE_NCL
            size_t instanceOffset = 0;
            for(int inst=0; inst<instanceCount; inst++) {
#ifdef HAVE_PLL
                if (!pllOnly) {
#endif
                beagleSetTipStates(instances[inst], i, tmpStates + instanceOffset);
#ifdef HAVE_PLL
                } //if (!pllOnly)
#endif
                instanceOffset += instanceSitesCount[inst];
            }
#ifdef HAVE_PLL
            if (pllTest) {
                char* pll_tmp_states = pll_getNucleotideCharStates(tmpStates, nsites);
                pll_set_tip_states(pll_partition, i, pll_map_nt, pll_tmp_states);
                free(pll_tmp_states);
            }
#endif // HAVE_PLL
            free(tmpStates);                
        }
    }


    double* rates = (double*) malloc(rateCategoryCount * sizeof(double));

    
    setNewCategoryRates(partitionCount, rateCategoryCount, instanceCount, instances,
#ifdef HAVE_PLL
                        pllTest, pllOnly, pll_partition,
#endif
                        (double*) rates);
    
    double* patternWeights = (double*) malloc(sizeof(double) * nsites);

    setNewPatternWeights(nsites, instanceCount, instances, instanceSitesCount,
#ifdef HAVE_PLL
                         pllTest, pllOnly, pll_partition,
#endif
                         (double*) patternWeights);

    int* patternPartitions;
    double* partitionLogLs;
    double* partitionD1;
    double* partitionD2;
    if (partitionCount > 1) {
        partitionLogLs = (double*) malloc(sizeof(double) * partitionCount);
        partitionD1 = (double*) malloc(sizeof(double) * partitionCount);
        partitionD2 = (double*) malloc(sizeof(double) * partitionCount);
        patternPartitions = (int*) malloc(sizeof(int) * nsites);
        int partitionSize = nsites/partitionCount;
        for (int i = 0; i < nsites; i++) {
            int sitePartition =  gt_rand()%partitionCount;
            // int sitePartition =  i%partitionCount;
            // int sitePartition = i/partitionSize;
            if (sitePartition > partitionCount - 1)
                sitePartition = partitionCount - 1;
            patternPartitions[i] = sitePartition;
            // printf("patternPartitions[%d] = %d\n", i, patternPartitions[i]);
        }    
        // beagleSetPatternPartitions(instances[0], partitionCount, patternPartitions);
    }

    gt_srand(randomSeed);   // reset the random seed...

    // create base frequency array

	double* freqs = (double*) malloc(stateCount * sizeof(double));
    
    // create an array containing site category weights
	double* weights = (double*) malloc(rateCategoryCount * sizeof(double));

    setNewCategoryWeights(eigenCount, rateCategoryCount, instanceCount, instances,
#ifdef HAVE_PLL
                          pllTest, pllOnly, pll_partition,
#endif
                          (double*) weights);
    
    setNewEigenModels(modelCount, stateCount, (double*) freqs,
                      eigencomplex, ievectrans, setmatrix, eigenCount,
                      instanceCount,
#ifdef HAVE_PLL
                      pllTest, pllOnly, pll_partition,
#endif
                      instances);
    
    // a list of indices and edge lengths
    int* edgeIndices = new int[edgeCount*modelCount];
    int* edgeIndicesD1 = new int[edgeCount*modelCount];
    int* edgeIndicesD2 = new int[edgeCount*modelCount];

#ifdef HAVE_PLL
    unsigned int* pll_edgeIndices;
    if (pllTest) {
        pll_edgeIndices = new unsigned int[edgeCount];
    }
#endif // HAVE_PLL

    for(int i=0; i<edgeCount*modelCount; i++) {
        edgeIndices[i]=i;

#ifdef HAVE_PLL
        if (pllTest) {
            pll_edgeIndices[i] = i;
        }
#endif // HAVE_PLL

        edgeIndicesD1[i]=(edgeCount*modelCount)+i;
        edgeIndicesD2[i]=2*(edgeCount*modelCount)+i;
    }
    
    double* edgeLengths = new double[edgeCount*modelCount];
    for(int i=0; i<edgeCount; i++) {
        edgeLengths[i]=gt_rand() / (double) GT_RAND_MAX;
    }
    
    // create a list of partial likelihood update operations
    // the order is [dest, destScaling, source1, matrix1, source2, matrix2]
    int operationCount = internalCount*modelCount;
    int beagleOpCount = BEAGLE_OP_COUNT;
    if (partitionCount > 1)
        beagleOpCount = BEAGLE_PARTITION_OP_COUNT;
    int* operations = new int[beagleOpCount*operationCount];
    int unpartOpsCount = internalCount*eigenCount;
    int* scalingFactorsIndices = new int[unpartOpsCount]; // internal nodes

#ifdef HAVE_PLL
    if (pllTest) {
        pll_operations = (pll_operation_t *)malloc(unpartOpsCount* sizeof(pll_operation_t));
    }
#endif // HAVE_PLL

    for(int i=0; i<unpartOpsCount; i++){
        int child1Index;
        if (((i % internalCount)*2) < ntaxa)
            child1Index = (i % internalCount)*2;
        else
            child1Index = i*2 - internalCount * (int)(i / internalCount);
        int child2Index;
        if (((i % internalCount)*2+1) < ntaxa)
            child2Index = (i % internalCount)*2+1;
        else
            child2Index = i*2+1 - internalCount * (int)(i / internalCount);

        for (int j=0; j<partitionCount; j++) {
            int op = partitionCount*i + j;
            operations[op*beagleOpCount+0] = ntaxa+i;
            operations[op*beagleOpCount+1] = (dynamicScaling ? i : BEAGLE_OP_NONE);
            operations[op*beagleOpCount+2] = (dynamicScaling ? i : BEAGLE_OP_NONE);
            operations[op*beagleOpCount+3] = child1Index;
            operations[op*beagleOpCount+4] = child1Index + j*edgeCount;
            operations[op*beagleOpCount+5] = child2Index;
            operations[op*beagleOpCount+6] = child2Index + j*edgeCount;
            if (partitionCount > 1) {
                operations[op*beagleOpCount+7] = j;
                operations[op*beagleOpCount+8] = (dynamicScaling ? internalCount : BEAGLE_OP_NONE);
            }

#ifdef HAVE_PLL
            if (pllTest) {
                pll_operations[op].parent_clv_index    = ntaxa+i;
                pll_operations[op].child1_clv_index    = child1Index;
                pll_operations[op].child2_clv_index    = child2Index;
                pll_operations[op].child1_matrix_index = child1Index + j*edgeCount;
                pll_operations[op].child2_matrix_index = child2Index + j*edgeCount;
                pll_operations[op].parent_scaler_index = PLL_SCALE_BUFFER_NONE;
                pll_operations[op].child1_scaler_index = PLL_SCALE_BUFFER_NONE;
                pll_operations[op].child2_scaler_index = PLL_SCALE_BUFFER_NONE;
            }
#endif // HAVE_PLL

            // printf("op %d i %d j %d dest %d c1 %d c2 %d c1m %d c2m %d p %d\n",
            //        op, i, j, ntaxa+i, child1Index, child2Index,
            //        operations[op*beagleOpCount+4], operations[op*beagleOpCount+6], j);
        }

        scalingFactorsIndices[i] = i;

        if (autoScaling)
            scalingFactorsIndices[i] += ntaxa;
    }   

    int* rootIndices = new int[eigenCount * partitionCount];
    int* lastTipIndices = new int[eigenCount * partitionCount];
    int* lastTipIndicesD1 = new int[eigenCount * partitionCount];
    int* lastTipIndicesD2 = new int[eigenCount * partitionCount];
    int* categoryWeightsIndices = new int[eigenCount * partitionCount];
    int* stateFrequencyIndices = new int[eigenCount * partitionCount];
    int* cumulativeScalingFactorIndices = new int[eigenCount * partitionCount];
    int* partitionIndices = new int[partitionCount];
    
    for (int eigenIndex=0; eigenIndex < eigenCount; eigenIndex++) {
        int pOffset = partitionCount*eigenIndex;

        for (int partitionIndex=0; partitionIndex < partitionCount; partitionIndex++) {
            if (eigenIndex == 0)
                partitionIndices[partitionIndex] = partitionIndex;
            rootIndices[partitionIndex + pOffset] = ntaxa+(internalCount*(eigenIndex+1))-1;//ntaxa*2-2;
            lastTipIndices[partitionIndex + pOffset] = ntaxa-1;
            lastTipIndicesD1[partitionIndex + pOffset] = (ntaxa-1) + (edgeCount*modelCount);
            lastTipIndicesD2[partitionIndex + pOffset] = (ntaxa-1) + 2*(edgeCount*modelCount);
            categoryWeightsIndices[partitionIndex + pOffset] = eigenIndex;
            stateFrequencyIndices[partitionIndex + pOffset] = 0;
            cumulativeScalingFactorIndices[partitionIndex + pOffset] = ((manualScaling || dynamicScaling) ? (scaleCount*eigenCount-1)-eigenCount+eigenIndex+1 : BEAGLE_OP_NONE);
        }

        if (dynamicScaling) {
#ifdef HAVE_PLL
            if (!pllOnly) {
#endif
            beagleResetScaleFactors(instances[0], cumulativeScalingFactorIndices[eigenIndex]);
#ifdef HAVE_PLL
            } //if (!pllOnly)
#endif
        }
    }

    struct timeval time0, time1, time2, time3, time4, time5;
    double bestTimeSetPartitions, bestTimeUpdateTransitionMatrices, bestTimeUpdatePartials, bestTimeAccumulateScaleFactors, bestTimeCalculateRootLogLikelihoods, bestTimeTotal;

    int timePrecision = 4;
    int speedupPrecision = 2;
    int percentPrecision = 2;
    
    double logL = 0.0;
    double deriv1 = 0.0;
    double deriv2 = 0.0;
    
    double previousLogL = 0.0;
    double previousDeriv1 = 0.0;
    double previousDeriv2 = 0.0;

    int* eigenIndices = new int[edgeCount * modelCount];
    int* categoryRateIndices = new int[edgeCount * modelCount];
    for (int eigenIndex=0; eigenIndex < modelCount; eigenIndex++) {
        for(int j=0; j<edgeCount; j++) {
            eigenIndices[eigenIndex*edgeCount + j] = eigenIndex;
            categoryRateIndices[eigenIndex*edgeCount + j] = eigenIndex;
            edgeLengths[eigenIndex*edgeCount + j] = edgeLengths[j];
        }
    }

    gt_srand(randomSeed);   // reset the random seed...

#ifdef HAVE_PLL
    if (!pllOnly) {
#endif

    if ((treenewick || randomTree) && eigenCount==1 && !unrooted) {
        generateNewTree(ntaxa, rerootTrees, pectinate, postorderTraversal, dynamicScaling,
            edgeCount, internalCount, unpartOpsCount, partitionCount, beagleOpCount,
#ifdef HAVE_PLL
            pllTest, pll_operations,
#endif
#ifdef HAVE_NCL
            (newTreePerRep ? NULL : treenewick),
#endif 
            operations);
    }

    int numThreads = 0;
    threadData* threads;
    std::shared_future<void>* futures;
    std::vector<double> threadLogL;

    if (clientThreadingEnabled) {
        numThreads = instanceCount;
        threads = new threadData[numThreads];
        for (int i = 0; i < numThreads; i++) {
            threads[i].t = std::thread(threadWaiting, &threads[i]);
            threadLogL.push_back(0.0);
        }
        futures = new std::shared_future<void>[numThreads];
        if (futures == NULL)
            throw std::bad_alloc();
    }

    auto computeLikelihood = [&] (int i,
                                  double* replicateLogL,
                                  int replicateInstanceCount,
                                  int* replicateInstances,
                                  int* replicateInstanceSitesCount) {

        if (partitionCount > 1 && i==0) { //!(i % rescaleFrequency)) {
            if (beagleSetPatternPartitions(replicateInstances[0], partitionCount, patternPartitions) != BEAGLE_SUCCESS) {
                printf("ERROR: No BEAGLE implementation for beagleSetPatternPartitions\n");
                exit(-1);
            }
        }

        gettimeofday(&time1,NULL);

        if (partitionCount > 1) {
            int totalEdgeCount = edgeCount * modelCount;
            beagleUpdateTransitionMatricesWithMultipleModels(
                                           replicateInstances[0],     // instance
                                           eigenIndices,   // eigenIndex
                                           categoryRateIndices,   // category rate index
                                           edgeIndices,   // probabilityIndices
                                           (calcderivs ? edgeIndicesD1 : NULL), // firstDerivativeIndices
                                           (calcderivs ? edgeIndicesD2 : NULL), // secondDerivativeIndices
                                           edgeLengths,   // edgeLengths
                                           totalEdgeCount);            // count
        } else {
            for (int eigenIndex=0; eigenIndex < modelCount; eigenIndex++) {
                if (!setmatrix) {
                    for(int inst=0; inst<replicateInstanceCount; inst++) {
                        // tell BEAGLE to populate the transition matrices for the above edge lengths
                        beagleUpdateTransitionMatrices(replicateInstances[inst],     // instance
                                                       eigenIndex,             // eigenIndex
                                                       &edgeIndices[eigenIndex*edgeCount],   // probabilityIndices
                                                       (calcderivs ? &edgeIndicesD1[eigenIndex*edgeCount] : NULL), // firstDerivativeIndices
                                                       (calcderivs ? &edgeIndicesD2[eigenIndex*edgeCount] : NULL), // secondDerivativeIndices
                                                       edgeLengths,   // edgeLengths
                                                       edgeCount);            // count
                    }

                } else {
                    double* inMatrix = new double[stateCount*stateCount*rateCategoryCount];
                    for (int matrixIndex=0; matrixIndex < edgeCount; matrixIndex++) {
                        for(int z=0;z<rateCategoryCount;z++){
                            for(int x=0;x<stateCount;x++){
                                for(int y=0;y<stateCount;y++){
                                    inMatrix[z*stateCount*stateCount + x*stateCount + y] = gt_rand() / (double) GT_RAND_MAX;
                                }
                            } 
                        }
                        beagleSetTransitionMatrix(replicateInstances[0], edgeIndices[eigenIndex*edgeCount + matrixIndex], inMatrix, 1);
                        if (calcderivs) {
                            beagleSetTransitionMatrix(replicateInstances[0], edgeIndicesD1[eigenIndex*edgeCount + matrixIndex], inMatrix, 0);
                            beagleSetTransitionMatrix(replicateInstances[0], edgeIndicesD2[eigenIndex*edgeCount + matrixIndex], inMatrix, 0);
                        }
                    }
                }
            }

        }

        // std::cout.setf(std::ios::showpoint);
        // // std::cout.setf(std::ios::floatfield, std::ios::fixed);
        // std::cout.precision(4);
        // unsigned int partialsOps = internalCount * eigenCount;
        // unsigned int flopsPerPartial = (stateCount * 4) - 2 + 1;
        // unsigned long long partialsSize = stateCount * nsites * rateCategoryCount;
        // unsigned long long partialsTotal = partialsSize * partialsOps;
        // unsigned long long flopsTotal = partialsTotal * flopsPerPartial;

        // std::cout << " compute throughput:   ";

        // for (int pRep=0; pRep < 50; pRep++) {
            gettimeofday(&time2, NULL);

            // update the partials
            if (partitionCount > 1) {
                beagleUpdatePartialsByPartition( replicateInstances[0],                   // instance
                                (BeagleOperationByPartition*)operations,     // operations
                                internalCount*eigenCount*partitionCount);    // operationCount
            } else {
                for(int inst=0; inst<replicateInstanceCount; inst++) {
                    beagleUpdatePartials( replicateInstances[inst],      // instance
                                    (BeagleOperation*)operations,     // operations
                                    internalCount*eigenCount,              // operationCount
                                    (dynamicScaling ? internalCount : BEAGLE_OP_NONE));             // cumulative scaling index
                }
            }

            gettimeofday(&time3, NULL);

            // struct timespec ts;
            // ts.tv_sec = 0;
            // ts.tv_nsec = 100000000;
            // nanosleep(&ts, NULL);

            // std::cout << (flopsTotal/getTimeDiff(time2, time3))/1000000.0 << ", ";
        // }
        // std::cout << " GFLOPS " << std::endl<< std::endl;

        // std::cout << " compute throughput:   " << (flopsTotal/getTimeDiff(time2, time3))/1000000.0 << " GFLOPS " << std::endl;


        int scalingFactorsCount = internalCount;
                
        for (int eigenIndex=0; eigenIndex < eigenCount; eigenIndex++) {
            if (manualScaling && !(i % rescaleFrequency)) {
                beagleResetScaleFactors(replicateInstances[0],
                                        cumulativeScalingFactorIndices[eigenIndex]);
                
                beagleAccumulateScaleFactors(replicateInstances[0],
                                       &scalingFactorsIndices[eigenIndex*internalCount],
                                       scalingFactorsCount,
                                       cumulativeScalingFactorIndices[eigenIndex]);
            } else if (autoScaling) {
                beagleAccumulateScaleFactors(replicateInstances[0], &scalingFactorsIndices[eigenIndex*internalCount], scalingFactorsCount, BEAGLE_OP_NONE);
            }
        }
        
        gettimeofday(&time4, NULL);

        // calculate the site likelihoods at the root node
        if (!unrooted) {
            if (partitionCount > 1) {
                beagleCalculateRootLogLikelihoodsByPartition(
                                            replicateInstances[0],               // instance
                                            rootIndices,// bufferIndices
                                            categoryWeightsIndices,                // weights
                                            stateFrequencyIndices,                 // stateFrequencies
                                            cumulativeScalingFactorIndices,
                                            partitionIndices,
                                            partitionCount,
                                            eigenCount,                      // count
                                            partitionLogLs,
                                            replicateLogL);         // outLogLikelihoods
            } else {
                for(int inst=0; inst<replicateInstanceCount; inst++) {
                    beagleCalculateRootLogLikelihoods(replicateInstances[inst],               // instance
                                                rootIndices,// bufferIndices
                                                categoryWeightsIndices,                // weights
                                                stateFrequencyIndices,                 // stateFrequencies
                                                cumulativeScalingFactorIndices,
                                                eigenCount,                      // count
                                                replicateLogL);         // outLogLikelihoods
                }
                if (multiRsrc && !clientThreadingEnabled) {
                    *replicateLogL = 0.0;
                    double instLogL;
                    for(int inst=0; inst<replicateInstanceCount; inst++) {
                        beagleGetLogLikelihood(inst,
                                               &instLogL);
                        *replicateLogL += instLogL;
                    }
                }
            }
        } else {
            if (partitionCount > 1) {
                beagleCalculateEdgeLogLikelihoodsByPartition(
                                                  replicateInstances[0],
                                                  rootIndices,
                                                  lastTipIndices,
                                                  lastTipIndices,
                                                  (calcderivs ? lastTipIndicesD1 : NULL),
                                                  (calcderivs ? lastTipIndicesD2 : NULL),
                                                  categoryWeightsIndices,
                                                  stateFrequencyIndices,
                                                  cumulativeScalingFactorIndices,
                                                  partitionIndices,
                                                  partitionCount,
                                                  eigenCount,
                                                  partitionLogLs,
                                                  replicateLogL,
                                                  (calcderivs ? partitionD1 : NULL),
                                                  (calcderivs ? &deriv1 : NULL),
                                                  (calcderivs ? partitionD2 : NULL),
                                                  (calcderivs ? &deriv2 : NULL));
            } else {
                for(int inst=0; inst<replicateInstanceCount; inst++) {
                    beagleCalculateEdgeLogLikelihoods(replicateInstances[inst],               // instance
                                                      rootIndices,// bufferIndices
                                                      lastTipIndices,
                                                      lastTipIndices,
                                                      (calcderivs ? lastTipIndicesD1 : NULL),
                                                      (calcderivs ? lastTipIndicesD2 : NULL),
                                                      categoryWeightsIndices,                // weights
                                                      stateFrequencyIndices,                 // stateFrequencies
                                                      cumulativeScalingFactorIndices,
                                                      eigenCount,                      // count
                                                      replicateLogL,    // outLogLikelihood
                                                      (calcderivs ? &deriv1 : NULL),
                                                      (calcderivs ? &deriv2 : NULL));
                }
                if (multiRsrc && !clientThreadingEnabled) {
                    *replicateLogL = 0.0;
                    double instLogL;
                    for(int inst=0; inst<replicateInstanceCount; inst++) {
                        beagleGetLogLikelihood(inst,
                                               &instLogL);
                        *replicateLogL += instLogL;
                    }
                    if (calcderivs) {
                        deriv1 = deriv2 = 0.0;
                        double instDeriv1, instDeriv2;
                        for(int inst=0; inst<replicateInstanceCount; inst++) {
                            beagleGetDerivatives(inst,
                                                 &instDeriv1,
                                                 &instDeriv2);
                            deriv1 += instDeriv1;
                            deriv2 += instDeriv2;
                        }
                    }
                }
            }

        }
    }; // end lambda

//  replicate loop
    for (int i=0; i<nreps; i++){

        if (newDataPerRep) {
            for(int ii=0; ii<ntaxa; ii++)
            {
                if (compactTipCount == 0 || (ii >= (compactTipCount-1) && ii != (ntaxa-1))) {
                    double* tmpPartials = getRandomTipPartials(nsites, stateCount);
                    beagleSetTipPartials(instances[0], ii, tmpPartials);
                    free(tmpPartials);
                } else {
                    int* tmpStates = getRandomTipStates(nsites, stateCount);
                    beagleSetTipStates(instances[0], ii, tmpStates);
                    free(tmpStates);                
                }
            }
        }

        if (newTreePerRep && i > 0 && i != (nreps-1)) {
            generateNewTree(ntaxa, rerootTrees, pectinate, postorderTraversal, dynamicScaling,
                edgeCount, internalCount, unpartOpsCount, partitionCount, beagleOpCount,
#ifdef HAVE_PLL
                false, pll_operations,
#endif
#ifdef HAVE_NCL
                NULL,
#endif 
                operations);

            for(int j=0; j<edgeCount; j++) {
                edgeLengths[j] = gt_rand() / (double) GT_RAND_MAX;
            }
        } else if (newTreePerRep && treenewick && i == (nreps-1)) {
            generateNewTree(ntaxa, rerootTrees, pectinate, postorderTraversal, dynamicScaling,
                edgeCount, internalCount, unpartOpsCount, partitionCount, beagleOpCount,
    #ifdef HAVE_PLL
                pllTest, pll_operations,
    #endif
    #ifdef HAVE_NCL
                treenewick,
    #endif 
                operations);

            for(int j=0; j<edgeCount; j++) {
                edgeLengths[j] = gt_rand() / (double) GT_RAND_MAX;
            }
        }

        if (newParametersPerRep) {
            setNewCategoryRates(partitionCount, rateCategoryCount, instanceCount, instances,
#ifdef HAVE_PLL
                                false, false, pll_partition,
#endif
                                (double*) rates);

            setNewPatternWeights(nsites, instanceCount, instances, instanceSitesCount,
#ifdef HAVE_PLL
                                 false, false, pll_partition,
#endif
                                 (double*) patternWeights);

            setNewCategoryWeights(eigenCount, rateCategoryCount, instanceCount, instances,
#ifdef HAVE_PLL
                                  false, false, pll_partition,
#endif
                                  (double*) weights);
            
            setNewEigenModels(modelCount, stateCount, (double*) freqs,
                              eigencomplex, ievectrans, setmatrix, eigenCount,
                              instanceCount,
#ifdef HAVE_PLL
                              false, false, pll_partition,
#endif
                              instances);
        }


        if (manualScaling && (!(i % rescaleFrequency) || !((i-1) % rescaleFrequency))) {
            for(int j=0; j<operationCount; j++){
                int sIndex = j / partitionCount;
                operations[beagleOpCount*j+1] = (((manualScaling && !(i % rescaleFrequency))) ? sIndex : BEAGLE_OP_NONE);
                operations[beagleOpCount*j+2] = (((manualScaling && (i % rescaleFrequency))) ? sIndex : BEAGLE_OP_NONE);
            }
        }

        if (clientThreadingEnabled) {
            for (int j=0; j<numThreads; j++) {

                std::packaged_task<void()> threadTask(
                    std::bind(computeLikelihood, i, &threadLogL[j], 1,
                        &instances[j], &instanceSitesCount[j]));

                futures[j] = threadTask.get_future();
                threadData* td = &threads[j];

                std::unique_lock<std::mutex> l(td->m);
                td->jobs.push(std::move(threadTask));
                l.unlock();

            }

            logL = 0.0;
        }
        
        // start timing!
        gettimeofday(&time0,NULL);

        if (clientThreadingEnabled) {
            for (int j=0; j<numThreads; j++) {
                threads[j].cv.notify_one();

            }
            for (int j=0; j<numThreads; j++) {
                futures[j].wait();
                logL += threadLogL[j];
            }
        } else {
            computeLikelihood(i, &logL, instanceCount, &instances[0], &instanceSitesCount[0]);
        }

        // end timing!
        gettimeofday(&time5,NULL);
        
        // std::cout.setf(std::ios::showpoint);
        // std::cout.setf(std::ios::floatfield, std::ios::fixed);
        // int timePrecision = 6;
        // int speedupPrecision = 2;
        // int percentPrecision = 2;
        // std::cout << "run " << i << ": ";
        // printTiming(getTimeDiff(time1, time5), timePrecision, resource, cpuTimeTotal, speedupPrecision, 0, 0, 0);
        // fprintf(stdout, "logL = %.5f  ", logL);

            // unsigned int partialsOps = internalCount * eigenCount;
            // unsigned int flopsPerPartial = (stateCount * 4) - 2 + 1;
            // unsigned long long partialsSize = stateCount * nsites * rateCategoryCount;
            // unsigned long long partialsTotal = partialsSize * partialsOps;
            // unsigned long long flopsTotal = partialsTotal * flopsPerPartial;
            // std::cout << " compute throughput:   " << (flopsTotal/getTimeDiff(time2, time3))/1000000.0 << " GFLOPS " << std::endl;
    
        if (i == 0 || getTimeDiff(time0, time5) < bestTimeTotal || (treenewick && i == (nreps-1)))  {
            bestTimeTotal = getTimeDiff(time0, time5);
            bestTimeSetPartitions = getTimeDiff(time0, time1);
            bestTimeUpdateTransitionMatrices = getTimeDiff(time1, time2);
            bestTimeUpdatePartials = getTimeDiff(time2, time3);
            bestTimeAccumulateScaleFactors = getTimeDiff(time3, time4);
            bestTimeCalculateRootLogLikelihoods = getTimeDiff(time4, time5);
        }
        
        if (!(logL - logL == 0.0))
            fprintf(stdout, "error: invalid lnL\n");

        if (!newDataPerRep && !newTreePerRep && !newParametersPerRep) {        
            if (i > 0 && std::abs(logL - previousLogL) > MAX_DIFF)
                fprintf(stdout, "error: large lnL difference between reps\n");
        }
        
        if (calcderivs) {
            if (!(deriv1 - deriv1 == 0.0) || !(deriv2 - deriv2 == 0.0))
                fprintf(stdout, "error: invalid deriv\n");
            
            if (i > 0 && ((std::abs(deriv1 - previousDeriv1) > MAX_DIFF) || (std::abs(deriv2 - previousDeriv2) > MAX_DIFF)) )
                fprintf(stdout, "error: large deriv difference between reps\n");
        }

        previousLogL = logL;
        previousDeriv1 = deriv1;
        previousDeriv2 = deriv2;        
    }

    if (clientThreadingEnabled) {
        // Send stop signal to all threads and join them...
        for (int i = 0; i < numThreads; i++) {
            threadData* td = &threads[i];
            std::unique_lock<std::mutex> l(td->m);
            td->stop = true;
            td->cv.notify_one();
        }

        // Join all the threads
        for (int i = 0; i < numThreads; i++) {
            threadData* td = &threads[i];
            td->t.join();
        }

        delete[] threads;
        delete[] futures;
    }

    if (resource == 0) {
        cpuTimeSetPartitions = bestTimeSetPartitions;
        cpuTimeUpdateTransitionMatrices = bestTimeUpdateTransitionMatrices;
        cpuTimeUpdatePartials = bestTimeUpdatePartials;
        cpuTimeAccumulateScaleFactors = bestTimeAccumulateScaleFactors;
        cpuTimeCalculateRootLogLikelihoods = bestTimeCalculateRootLogLikelihoods;
        cpuTimeTotal = bestTimeTotal;
    }
    
    if (!calcderivs)
        fprintf(stdout, "logL = %.5f \n", logL);
    else
        fprintf(stdout, "logL = %.5f d1 = %.5f d2 = %.5f\n", logL, deriv1, deriv2);

    if (partitionCount > 1) {
        fprintf(stdout, " (");
        for (int p=0; p < partitionCount; p++) {
            fprintf(stdout, "p%d = %.5f", p, partitionLogLs[p]);
            if (p < partitionCount - 1)
                fprintf(stdout, ", ");
        }
        fprintf(stdout, ")\n");
    }
    
    if (calcderivs) {
        if (partitionCount > 1) {
            fprintf(stdout, " (");
            for (int p=0; p < partitionCount; p++) {
                fprintf(stdout, "p%dD1 = %.5f", p, partitionD1[p]);
                if (p < partitionCount - 1)
                    fprintf(stdout, ", ");
            }
            fprintf(stdout, ")\n");
        }
        
        if (partitionCount > 1) {
            fprintf(stdout, " (");
            for (int p=0; p < partitionCount; p++) {
                fprintf(stdout, "p%dD2 = %.5f", p, partitionD2[p]);
                if (p < partitionCount - 1)
                    fprintf(stdout, ", ");
            }
            fprintf(stdout, ")\n");
        }
    }

    if (sitelikes) {
        double* siteLogLs = (double*) malloc(sizeof(double) * nsites);
        beagleGetSiteLogLikelihoods(instances[0], siteLogLs);
        double sumLogL = 0.0;
        fprintf(stdout, "site likelihoods = ");
        for (int i=0; i<nsites; i++) {
            fprintf(stdout, "%.5f \t", siteLogLs[i]);
            sumLogL += siteLogLs[i] * patternWeights[i];
        }
        fprintf(stdout, "\nsumLogL = %.5f\n", sumLogL);
        if (calcderivs) {
            double* siteSecondDerivs = (double*) malloc(sizeof(double) * nsites);
            beagleGetSiteDerivatives(instances[0], siteLogLs, siteSecondDerivs);
            sumLogL = 0.0;
            fprintf(stdout, "site first derivs = ");
            for (int i=0; i<nsites; i++) {
                fprintf(stdout, "%.5f \t", siteLogLs[i]);
                sumLogL += siteLogLs[i] * patternWeights[i];
            }
            fprintf(stdout, "\nsumFirstDerivs = %.5f\n", sumLogL);

            sumLogL = 0.0;
            fprintf(stdout, "site second derivs = ");
            for (int i=0; i<nsites; i++) {
                fprintf(stdout, "%.5f \t", siteSecondDerivs[i]);
                sumLogL += siteSecondDerivs[i] * patternWeights[i];
            }
            fprintf(stdout, "\nsumSecondDerivs = %.5f\n", sumLogL);
            free(siteSecondDerivs);
        }
        free(siteLogLs);
    }

    if (partitionCount > 1) {
        free(patternPartitions);
    }

    std::cout.setf(std::ios::showpoint);
    std::cout.setf(std::ios::floatfield, std::ios::fixed);
    std::cout << "best run: ";
    printTiming(bestTimeTotal, timePrecision, resource, cpuTimeTotal, speedupPrecision, 0, 0, 0);
    if (fullTiming) {
        std::cout << " setPartitions:  ";
        printTiming(bestTimeSetPartitions, timePrecision, resource, cpuTimeSetPartitions, speedupPrecision, 1, bestTimeTotal, percentPrecision);
        std::cout << " transMats:  ";
        printTiming(bestTimeUpdateTransitionMatrices, timePrecision, resource, cpuTimeUpdateTransitionMatrices, speedupPrecision, 1, bestTimeTotal, percentPrecision);
        std::cout << " partials:   ";
        printTiming(bestTimeUpdatePartials, timePrecision, resource, cpuTimeUpdatePartials, speedupPrecision, 1, bestTimeTotal, percentPrecision);
        unsigned int partialsOps = internalCount * eigenCount;
        unsigned int flopsPerPartial = (stateCount * 4) - 2 + 1;
        unsigned int bytesPerPartial = 3 * (requireDoublePrecision ? 8 : 4);
        if (manualScaling) {
            flopsPerPartial++;
            bytesPerPartial += (requireDoublePrecision ? 8 : 4);
        }
        unsigned int matrixBytes = partialsOps * 2 * stateCount*stateCount*rateCategoryCount * (requireDoublePrecision ? 8 : 4);
        unsigned long long partialsSize = stateCount * nsites * rateCategoryCount;
        unsigned long long partialsTotal = partialsSize * partialsOps;
        unsigned long long flopsTotal = partialsTotal * flopsPerPartial;
        std::cout << " partials throughput:   " << (partialsTotal/bestTimeUpdatePartials)/1000.0 << " M partials/second " << std::endl;
        std::cout << " compute throughput:   " << (flopsTotal/bestTimeUpdatePartials)/1000000.0 << " GFLOPS " << std::endl;
        std::cout << " memory bandwidth:   " << (((partialsTotal * bytesPerPartial + matrixBytes)/bestTimeUpdatePartials))/1000000.0 << " GB/s " << std::endl;
        if (manualScaling || autoScaling) {
            std::cout << " accScalers: ";
            printTiming(bestTimeAccumulateScaleFactors, timePrecision, resource, cpuTimeAccumulateScaleFactors, speedupPrecision, 1, bestTimeTotal, percentPrecision);
        }
        std::cout << " rootLnL:    ";
        printTiming(bestTimeCalculateRootLogLikelihoods, timePrecision, resource, cpuTimeCalculateRootLogLikelihoods, speedupPrecision, 1, bestTimeTotal, percentPrecision);

        std::cout << " tree throughput total:   " << (partialsTotal/bestTimeTotal)/1000.0 << " M partials/second " << std::endl;

    }
    std::cout << "\n";
    
    for(int inst=0; inst<instanceCount; inst++) {
        beagleFinalizeInstance(instances[inst]);
    }
#ifdef HAVE_PLL
    } //if (!pllOnly)
#endif

//////////////////////////////////////////////////////////////////////////
// pll test
#ifdef HAVE_PLL
    if (pllTest) {

        double pll_bestTimeSetPartitions, pll_bestTimeUpdateTransitionMatrices, pll_bestTimeUpdatePartials, pll_bestTimeAccumulateScaleFactors, pll_bestTimeCalculateRootLogLikelihoods, pll_bestTimeTotal;

        logL = previousLogL = 0.0;

        gt_srand(randomSeed);   // reset the random seed...

        if ((treenewick || randomTree) && eigenCount==1 && !unrooted) {
            generateNewTree(ntaxa, rerootTrees, pectinate, postorderTraversal, dynamicScaling,
                edgeCount, internalCount, unpartOpsCount, partitionCount, beagleOpCount,
    #ifdef HAVE_PLL
                pllTest, pll_operations,
    #endif
    #ifdef HAVE_NCL
                (newTreePerRep ? NULL : treenewick),
    #endif 
                operations);
        }

        for (int i=0; i<nreps; i++){

            if (newDataPerRep) {
                for(int ii=0; ii<ntaxa; ii++)
                {
                    if (compactTipCount == 0 || (ii >= (compactTipCount-1) && ii != (ntaxa-1))) {
                        double* tmpPartials = getRandomTipPartials(nsites, stateCount);

                        pll_set_tip_clv(pll_partition, i, tmpPartials, 0);

                        free(tmpPartials);
                    } else {
                        int* tmpStates = getRandomTipStates(nsites, stateCount);

                        char* pll_tmp_states = pll_getNucleotideCharStates(tmpStates, nsites);
                        pll_set_tip_states(pll_partition, i, pll_map_nt, pll_tmp_states);
                        free(pll_tmp_states);

                        free(tmpStates);                
                    }
                }
            }


            if (newTreePerRep && randomTree && eigenCount==1 && !unrooted && i > 0 && i != (nreps-1)) {
                generateNewTree(ntaxa, rerootTrees, pectinate, postorderTraversal,
                    dynamicScaling,
                    edgeCount, internalCount, unpartOpsCount, partitionCount, beagleOpCount,
                    pllTest, pll_operations,
#ifdef HAVE_NCL
                    NULL,
#endif 
                    operations);

                for(int j=0; j<edgeCount; j++) {
                    edgeLengths[j] = gt_rand() / (double) GT_RAND_MAX;
                }
            } else if (newTreePerRep && treenewick && i == (nreps-1)) {
                generateNewTree(ntaxa, rerootTrees, pectinate, postorderTraversal, dynamicScaling,
                    edgeCount, internalCount, unpartOpsCount, partitionCount, beagleOpCount,
        #ifdef HAVE_PLL
                    pllTest, pll_operations,
        #endif
        #ifdef HAVE_NCL
                    treenewick,
        #endif 
                    operations);

                for(int j=0; j<edgeCount; j++) {
                    edgeLengths[j] = gt_rand() / (double) GT_RAND_MAX;
                }
            }

            if (newParametersPerRep) {
                setNewCategoryRates(partitionCount, rateCategoryCount, instanceCount, instances,
                                    pllTest, pllOnly, pll_partition,
                                    (double*) rates);

                setNewPatternWeights(nsites, instanceCount, instances, instanceSitesCount,
                                     pllTest, pllOnly, pll_partition,
                                     (double*) patternWeights);

                setNewCategoryWeights(eigenCount, rateCategoryCount, instanceCount, instances,
                                      pllTest, pllOnly, pll_partition,
                                      (double*) weights);
                
                setNewEigenModels(modelCount, stateCount, (double*) freqs,
                                  eigencomplex, ievectrans, setmatrix, eigenCount,
                                  instanceCount,
                                  pllTest, pllOnly, pll_partition,
                                  instances);
            }

            // TODO: pll scaling
            
            gettimeofday(&time0,NULL);

            // TODO: pll partitions

            gettimeofday(&time1,NULL);


            for (int eigenIndex=0; eigenIndex < modelCount; eigenIndex++) {
                // if (!setmatrix) {
                    // tell pll to populate the transition matrices for the above edge lengths
                    pll_update_prob_matrices(pll_partition,
                                             pll_params_indices,
                                             &pll_edgeIndices[eigenIndex*edgeCount],
                                             edgeLengths,
                                             edgeCount);
                // } 
            }
            
            // TODO: pll set matrix

            gettimeofday(&time2, NULL);

            pll_update_partials(pll_partition, pll_operations, internalCount*eigenCount);

            gettimeofday(&time3, NULL);

            // TODO: pll scaling
                    
            gettimeofday(&time4, NULL);

            unsigned int pll_rootIndex = rootIndices[0];
            unsigned int pll_lastTipIndex = lastTipIndices[0];

            // calculate the site likelihoods at the root node
            if (!unrooted) {

                logL = pll_compute_root_loglikelihood(pll_partition,
                                                      pll_rootIndex,
                                                      PLL_SCALE_BUFFER_NONE,
                                                      pll_params_indices,
                                                      NULL);
            } else {

                logL = pll_compute_edge_loglikelihood(pll_partition,
                                                              pll_rootIndex,
                                                              PLL_SCALE_BUFFER_NONE,
                                                              pll_lastTipIndex,
                                                              PLL_SCALE_BUFFER_NONE,
                                                              pll_lastTipIndex,
                                                              pll_params_indices,
                                                              NULL);
            }
            // end timing!
            gettimeofday(&time5,NULL);
            

            if (i == 0 || getTimeDiff(time0, time5) < pll_bestTimeTotal || (treenewick && i == (nreps-1))) {
                pll_bestTimeTotal = getTimeDiff(time0, time5);
                pll_bestTimeSetPartitions = getTimeDiff(time0, time1);
                pll_bestTimeUpdateTransitionMatrices = getTimeDiff(time1, time2);
                pll_bestTimeUpdatePartials = getTimeDiff(time2, time3);
                pll_bestTimeAccumulateScaleFactors = getTimeDiff(time3, time4);
                pll_bestTimeCalculateRootLogLikelihoods = getTimeDiff(time4, time5);
            }
            
            if (!(logL - logL == 0.0))
                fprintf(stdout, "pll error: invalid lnL\n");

            if (!newDataPerRep && !newTreePerRep && !newParametersPerRep) {        
                if (i > 0 && std::abs(logL - previousLogL) > MAX_DIFF)
                    fprintf(stdout, "pll error: large lnL difference between reps\n");
            }
            
            // TODO: pll derivatives

            previousLogL = logL;
        }

        if (resource == 0) {
            cpuTimeSetPartitions = pll_bestTimeSetPartitions;
            cpuTimeUpdateTransitionMatrices = pll_bestTimeUpdateTransitionMatrices;
            cpuTimeUpdatePartials = pll_bestTimeUpdatePartials;
            cpuTimeAccumulateScaleFactors = pll_bestTimeAccumulateScaleFactors;
            cpuTimeCalculateRootLogLikelihoods = pll_bestTimeCalculateRootLogLikelihoods;
            cpuTimeTotal = pll_bestTimeTotal;
        }

        
        fprintf(stdout, "pll logL = %.5f \n", logL);

        //TODO: pll site likelihoods
    
        free(patternWeights);

        std::cout.setf(std::ios::showpoint);
        std::cout.setf(std::ios::floatfield, std::ios::fixed);
        std::cout << "pll best run: ";
        pll_printTiming(pll_bestTimeTotal, bestTimeTotal, timePrecision, 1, cpuTimeTotal, speedupPrecision, 0, 0, 0);
        if (fullTiming) {
            std::cout << " setPartitions:  ";
            printTiming(pll_bestTimeSetPartitions, timePrecision, resource, cpuTimeSetPartitions, speedupPrecision, 1, pll_bestTimeTotal, percentPrecision);
            std::cout << " transMats:  ";
            printTiming(pll_bestTimeUpdateTransitionMatrices, timePrecision, resource, cpuTimeUpdateTransitionMatrices, speedupPrecision, 1, pll_bestTimeTotal, percentPrecision);
            std::cout << " partials:   ";
            printTiming(pll_bestTimeUpdatePartials, timePrecision, resource, cpuTimeUpdatePartials, speedupPrecision, 1, pll_bestTimeTotal, percentPrecision);
            unsigned int partialsOps = internalCount * eigenCount;
            unsigned int flopsPerPartial = (stateCount * 4) - 2 + 1;
            unsigned int bytesPerPartial = 3 * (requireDoublePrecision ? 8 : 4);
            if (manualScaling) {
                flopsPerPartial++;
                bytesPerPartial += (requireDoublePrecision ? 8 : 4);
            }
            unsigned int matrixBytes = partialsOps * 2 * stateCount*stateCount*rateCategoryCount * (requireDoublePrecision ? 8 : 4);
            unsigned long long partialsSize = stateCount * nsites * rateCategoryCount;
            unsigned long long partialsTotal = partialsSize * partialsOps;
            unsigned long long flopsTotal = partialsTotal * flopsPerPartial;
            std::cout << " partials throughput:   " << (partialsTotal/pll_bestTimeUpdatePartials)/1000.0 << " M partials/second " << std::endl;
            std::cout << " compute throughput:   " << (flopsTotal/pll_bestTimeUpdatePartials)/1000000.0 << " GFLOPS " << std::endl;
            std::cout << " memory bandwidth:   " << (((partialsTotal * bytesPerPartial + matrixBytes)/pll_bestTimeUpdatePartials))/1000000.0 << " GB/s " << std::endl;
            if (manualScaling || autoScaling) {
                std::cout << " accScalers: ";
                printTiming(pll_bestTimeAccumulateScaleFactors, timePrecision, resource, cpuTimeAccumulateScaleFactors, speedupPrecision, 1, pll_bestTimeTotal, percentPrecision);
            }
            std::cout << " rootLnL:    ";
            printTiming(pll_bestTimeCalculateRootLogLikelihoods, timePrecision, resource, cpuTimeCalculateRootLogLikelihoods, speedupPrecision, 1, pll_bestTimeTotal, percentPrecision);

            std::cout << " tree throughput total:   " << (partialsTotal/pll_bestTimeTotal)/1000.0 << " M partials/second " << std::endl;

        }
        std::cout << "\n";
        
        delete[] pll_edgeIndices;
        free(pll_operations);
        free(pll_params_indices);
        pll_partition_destroy(pll_partition);
    }
#endif // HAVE_PLL

	free(freqs);
	free(weights);
	free(rates);

    if (multiRsrc) {
        std::exit(0);
    }

}

void printResourceList() {
    // print version and citation info
    fprintf(stdout, "BEAGLE version %s\n", beagleGetVersion());
    fprintf(stdout, "%s\n", beagleGetCitation());     

    // print resource list
    BeagleResourceList* rList;
    rList = beagleGetResourceList();
    fprintf(stdout, "Available resources:\n");
    for (int i = 0; i < rList->length; i++) {
        fprintf(stdout, "\tResource %i:\n\t\tName : %s\n", i, rList->list[i].name);
        fprintf(stdout, "\t\tDesc : %s\n", rList->list[i].description);
        fprintf(stdout, "\t\tFlags:");
        printFlags(rList->list[i].supportFlags);
        fprintf(stdout, "\n");
    }    
    fprintf(stdout, "\n");
    std::exit(0);
}


void helpMessage() {
    std::cerr << "Usage:\n\n";
    std::cerr << "synthetictest [--help] [--resourcelist] [--benchmarklist] [--states <integer>] [--taxa <integer>] [--sites <integer>] [--rates <integer>] [--manualscale] [--autoscale] [--dynamicscale] [--rsrc <integer>] [--reps <integer>] [--doubleprecision] [--disablevector] [--enablethreads] [--compacttips <integer>] [--seed <integer>] [--rescalefrequency <integer>] [--fulltiming] [--unrooted] [--calcderivs] [--logscalers] [--eigencount <integer>] [--eigencomplex] [--ievectrans] [--setmatrix] [--opencl] [--partitions <integer>] [--sitelikes] [--newdata] [--randomtree] [--reroot] [--stdrand] [--pectinate] [--multirsrc] [--postorder] [--newtree] [--newparameters] [--threadcount] [--clientthreads]";
#ifdef HAVE_PLL
    std::cerr << " [--plltest]";
    std::cerr << " [--pllonly]";
    std::cerr << " [--pllrepeats]";
#endif // HAVE_PLL
#ifdef HAVE_NCL
    std::cerr << " [--alignmentdna]";
    std::cerr << " [--compress]";
    std::cerr << " [--tree]";
#endif // HAVE_NCL
    std::cerr << "\n\n";
    std::cerr << "If --help is specified, this usage message is shown\n\n";
    std::cerr << "If --manualscale, --autoscale, or --dynamicscale is specified, BEAGLE will rescale the partials during computation\n\n";
    std::cerr << "If --fulltiming is specified, you will see more detailed timing results (requires BEAGLE_DEBUG_SYNCH defined to report accurate values)\n\n";
    std::exit(0);
}

void interpretCommandLineParameters(int argc, const char* argv[],
                                    int* stateCount,
                                    int* ntaxa,
                                    int* nsites,
                                    bool* manualScaling,
                                    bool* autoScaling,
                                    bool* dynamicScaling,
                                    int* rateCategoryCount,
                                    std::vector<int>* rsrc,
                                    int* nreps,
                                    bool* fullTiming,
                                    bool* requireDoublePrecision,
                                    bool* disableVector,
                                    bool* enableThreads,
                                    int* compactTipCount,
                                    int* randomSeed,
                                    int* rescaleFrequency,
                                    bool* unrooted,
                                    bool* calcderivs,
                                    bool* logscalers,
                                    int* eigenCount,
                                    bool* eigencomplex,
                                    bool* ievectrans,
                                    bool* setmatrix,
                                    bool* opencl,
                                    int*  partitions,
                                    bool* sitelikes,
                                    bool* newDataPerRep,
                                    bool* randomTree,
                                    bool* rerootTrees,
                                    bool* pectinate,
                                    bool* benchmarklist,
                                    bool* pllTest,
                                    bool* pllSiteRepeats,
                                    bool* pllOnly,
                                    bool* multiRsrc,
                                    bool* postorderTraversal,
                                    bool* newTreePerRep,
                                    bool* newParametersPerRep,
                                    int* threadCount,
                                    char** alignmentdna,
                                    bool* compress,
                                    char** treenewick,
                                    bool* clientThreadingEnabled)    {
    bool expecting_stateCount = false;
    bool expecting_ntaxa = false;
    bool expecting_nsites = false;
    bool expecting_rateCategoryCount = false;
    bool expecting_nreps = false;
    bool expecting_rsrc = false;
    bool expecting_compactTipCount = false;
    bool expecting_seed = false;
    bool expecting_rescaleFrequency = false;
    bool expecting_eigenCount = false;
    bool expecting_partitions = false;
    bool expecting_threads = false;
    bool expecting_alignmentdna = false;
    bool expecting_treenewick = false;
    
    for (unsigned i = 1; i < argc; ++i) {
        std::string option = argv[i];
        
        if (expecting_stateCount) {
            *stateCount = (unsigned)atoi(option.c_str());
            expecting_stateCount = false;
        } else if (expecting_ntaxa) {
            *ntaxa = (unsigned)atoi(option.c_str());
            expecting_ntaxa = false;
        } else if (expecting_nsites) {
            *nsites = (unsigned)atoi(option.c_str());
            expecting_nsites = false;
        } else if (expecting_rateCategoryCount) {
            *rateCategoryCount = (unsigned)atoi(option.c_str());
            expecting_rateCategoryCount = false;
        } else if (expecting_rsrc) {
            std::stringstream ss(option);
            int j;
            while (ss >> j) {
                rsrc->push_back(j);
                if (ss.peek() == ',')
                    ss.ignore();
            }
            expecting_rsrc = false;            
        } else if (expecting_nreps) {
            *nreps = (unsigned)atoi(option.c_str());
            expecting_nreps = false;
        } else if (expecting_compactTipCount) {
            *compactTipCount = (unsigned)atoi(option.c_str());
            expecting_compactTipCount = false;
        } else if (expecting_seed) {
            *randomSeed = (unsigned)atoi(option.c_str());
            expecting_seed = false;
        } else if (expecting_rescaleFrequency) {
            *rescaleFrequency = (unsigned)atoi(option.c_str());
            expecting_rescaleFrequency = false;
        } else if (expecting_eigenCount) {
            *eigenCount = (unsigned)atoi(option.c_str());
            expecting_eigenCount = false;
        } else if (expecting_partitions) {
            *partitions = (unsigned)atoi(option.c_str());
            expecting_partitions = false;
        } else if (expecting_threads) {
            *threadCount = (unsigned)atoi(option.c_str());
            expecting_threads = false;
        } else if (expecting_alignmentdna) {
            *alignmentdna = (char*) malloc(sizeof(char) * sizeof(option.c_str()));
            strcpy(*alignmentdna, option.c_str());
            expecting_alignmentdna = false;
        } else if (expecting_treenewick) {
            *treenewick = (char*) malloc(sizeof(char) * sizeof(option.c_str()));
            strcpy(*treenewick, option.c_str());
            expecting_treenewick = false;
        } else if (option == "--help") {
            helpMessage();
        } else if (option == "--resourcelist") {
            printResourceList();
        } else if (option == "--benchmarklist") {
            *benchmarklist = true;
        } else if (option == "--manualscale") {
            *manualScaling = true;
        } else if (option == "--autoscale") {
            *autoScaling = true;
        } else if (option == "--dynamicscale") {
            *dynamicScaling = true;
        } else if (option == "--doubleprecision") {
            *requireDoublePrecision = true;
        } else if (option == "--states") {
            expecting_stateCount = true;
        } else if (option == "--taxa") {
            expecting_ntaxa = true;
        } else if (option == "--sites") {
            expecting_nsites = true;
        } else if (option == "--rates") {
            expecting_rateCategoryCount = true;
        } else if (option == "--rsrc") {
            expecting_rsrc = true;
        } else if (option == "--reps") {
            expecting_nreps = true;
        } else if (option == "--compacttips") {
            expecting_compactTipCount = true;
        } else if (option == "--rescalefrequency") {
            expecting_rescaleFrequency = true;
        } else if (option == "--seed") {
            expecting_seed = true;
        } else if (option == "--fulltiming") {
            *fullTiming = true;
        } else if (option == "--disablevector") {
            *disableVector = true;
        } else if (option == "--enablethreads") {
            *enableThreads = true;
        } else if (option == "--unrooted") {
            *unrooted = true;
        } else if (option == "--calcderivs") {
            *calcderivs = true;
        } else if (option == "--logscalers") {
            *logscalers = true;
        } else if (option == "--eigencount") {
            expecting_eigenCount = true;
        } else if (option == "--eigencomplex") {
            *eigencomplex = true;
        } else if (option == "--ievectrans") {
            *ievectrans = true;
        } else if (option == "--setmatrix") {
            *setmatrix = true;
        } else if (option == "--opencl") {
            *opencl = true;
        } else if (option == "--partitions") {
            expecting_partitions = true;
        } else if (option == "--sitelikes") {
            *sitelikes = true;
        } else if (option == "--newdata") {
            *newDataPerRep = true;
        } else if (option == "--randomtree") {
            *randomTree = true;
        } else if (option == "--stdrand") {
            useStdlibRand = true;
        } else if (option == "--reroot") {
            *rerootTrees = true;
        } else if (option == "--pectinate") {
            *pectinate = true;
#ifdef HAVE_PLL
        } else if (option == "--plltest") {
            *pllTest = true;
        } else if (option == "--pllrepeats") {
            *pllSiteRepeats = true;
        } else if (option == "--pllonly") {
            *pllOnly = true;
            *pllTest = true;
#endif // HAVE_PLL
        } else if (option == "--multirsrc") {
            *multiRsrc = true;
        } else if (option == "--postorder") {
            *postorderTraversal = true;
        } else if (option == "--newtree") {
            *newTreePerRep = true;
        } else if (option == "--newparameters") {
            *newParametersPerRep = true;
        } else if (option == "--threadcount") {
            expecting_threads = true;
#ifdef HAVE_NCL
        } else if (option == "--alignmentdna") {
            expecting_alignmentdna = true;
        } else if (option == "--compress") {
            *compress = true;
        } else if (option == "--tree") {
            expecting_treenewick = true;
#endif // HAVE_NCL
        } else if (option == "--clientthreads") {
            *clientThreadingEnabled = true;
        } else {
            std::string msg("Unknown command line parameter \"");
            msg.append(option);         
            abort(msg.c_str());
        }
    }
    
    if (expecting_stateCount)
        abort("read last command line option without finding value associated with --states");
    
    if (expecting_ntaxa)
        abort("read last command line option without finding value associated with --taxa");
    
    if (expecting_nsites)
        abort("read last command line option without finding value associated with --sites");
    
    if (expecting_rateCategoryCount)
        abort("read last command line option without finding value associated with --rates");

    if (expecting_rsrc)
        abort("read last command line option without finding value associated with --rsrc");
    
    if (expecting_nreps)
        abort("read last command line option without finding value associated with --reps");
    
    if (expecting_seed)
        abort("read last command line option without finding value associated with --seed");
    
    if (expecting_rescaleFrequency)
        abort("read last command line option without finding value associated with --rescalefrequency");

    if (expecting_compactTipCount)
        abort("read last command line option without finding value associated with --compacttips");

    if (expecting_eigenCount)
        abort("read last command line option without finding value associated with --eigencount");

    if (expecting_partitions)
        abort("read last command line option without finding value associated with --partitions");

    if (*stateCount < 2)
        abort("invalid number of states supplied on the command line");
        
    if (*ntaxa < 2)
        abort("invalid number of taxa supplied on the command line");
      
    if (*nsites < 1)
        abort("invalid number of sites supplied on the command line");
    
    if (*rateCategoryCount < 1)
        abort("invalid number of rates supplied on the command line");
        
    if (*nreps < 1)
        abort("invalid number of reps supplied on the command line");

    if (*randomSeed < 1)
        abort("invalid number for seed supplied on the command line");   
        
    if (*manualScaling && *rescaleFrequency < 1)
        abort("invalid number for rescalefrequency supplied on the command line");   
    
    if (*compactTipCount < 0 || *compactTipCount > *ntaxa)
        abort("invalid number for compacttips supplied on the command line");
    
    if (*calcderivs && !(*unrooted))
        abort("calcderivs option requires unrooted tree option");
    
    if (*eigenCount < 1)
        abort("invalid number for eigencount supplied on the command line");
    
    if (*eigencomplex && (*stateCount != 4 || *eigenCount != 1))
        abort("eigencomplex option only works with stateCount=4 and eigenCount=1");

    if (*partitions < 1 || *partitions > *nsites)
        abort("invalid number for partitions supplied on the command line");

    if (*randomTree && (*eigenCount!=1 || *unrooted))
        abort("random tree topology can only be used with eigencount=1 and rooted trees");

    if (*partitions > 1 && *multiRsrc)
        abort("multiple resources cannot be used with partitioning");

    if (*manualScaling && *multiRsrc)
        abort("multiple resources cannot be used with scaling");

    if (*newDataPerRep && *multiRsrc)
        abort("multiple resources cannot be used with new data per replicate");

    if (*setmatrix && *multiRsrc)
        abort("multiple resources cannot be used with arbitrary transition matrix setting");

    if (*sitelikes && *multiRsrc)
        abort("multiple resources cannot be used with site likelihoods output");

    if (*postorderTraversal && (*randomTree==false && treenewick==NULL))
        abort("postorder traversal can only be used with randomtree option");

    if (*newTreePerRep && *randomTree==false)
        abort("new tree per replicate can only be used with randomtree option");

    if (*newTreePerRep && *eigenCount!=1)
        abort("new tree per replicate can only be used with eigencount=1");

    if (*newTreePerRep && *unrooted)
        abort("new tree per replicate can only be used with rooted trees");

    if (*clientThreadingEnabled && *multiRsrc==false)
        abort("client-side threading requires 'multirsrc' setting to be enabled");
}

int main( int argc, const char* argv[] )
{
    // Default values
    int stateCount = 4;
    int ntaxa = 16;
    int nsites = 10000;
    bool manualScaling = false;
    bool autoScaling = false;
    bool dynamicScaling = false;
    bool requireDoublePrecision = false;
    bool disableVector = false;
    bool enableThreads = false;
    bool unrooted = false;
    bool calcderivs = false;
    int compactTipCount = 0;
    int randomSeed = 1;
    int rescaleFrequency = 1;
    bool logscalers = false;
    int eigenCount = 1;
    bool eigencomplex = false;
    bool ievectrans = false;
    bool setmatrix = false;
    bool opencl = false;
    bool sitelikes = false;
    int partitions = 1;
    bool newDataPerRep = false;
    bool randomTree = false;
    bool rerootTrees = false;
    bool pectinate = false;
    bool benchmarklist = false;
    bool pllTest = false;
    bool pllSiteRepeats = false;
    bool pllOnly = false;
    bool multiRsrc = false;
    bool postorderTraversal = false;
    bool newTreePerRep = false;
    bool newParametersPerRep = false;
    int threadCount = 1;
    useStdlibRand = false;
    char* alignmentdna = NULL;
    bool alignmentFromFile = false;
    bool compress = false;
    char* treenewick = NULL;
    bool clientThreadingEnabled = false;

    std::vector<int> rsrc;
    rsrc.push_back(-1);

    int* rsrcList  = NULL;
    int  rsrcCount = 0;

    int nreps = 5;
    bool fullTiming = false;
    
    int rateCategoryCount = 4;
    
    interpretCommandLineParameters(argc, argv, &stateCount, &ntaxa, &nsites, &manualScaling, &autoScaling,
                                   &dynamicScaling, &rateCategoryCount, &rsrc, &nreps, &fullTiming,
                                   &requireDoublePrecision, &disableVector, &enableThreads, &compactTipCount, &randomSeed,
                                   &rescaleFrequency, &unrooted, &calcderivs, &logscalers,
                                   &eigenCount, &eigencomplex, &ievectrans, &setmatrix, &opencl,
                                   &partitions, &sitelikes, &newDataPerRep, &randomTree, &rerootTrees, &pectinate, &benchmarklist, &pllTest, &pllSiteRepeats, &pllOnly, &multiRsrc,
                                   &postorderTraversal, &newTreePerRep, &newParametersPerRep,
                                   &threadCount, &alignmentdna, &compress, &treenewick,
                                   &clientThreadingEnabled);

    if (alignmentdna == NULL) {
        std::cout << "\nSimulating genomic ";
        if (stateCount == 4)
            std::cout << "DNA";
        else
            std::cout << stateCount << "-state data";
        if (partitions > 1) {
            std::cout << " with " << ntaxa << " taxa, " << nsites << " site patterns, and " << partitions << " partitions";
        } else {
            std::cout << " with " << ntaxa << " taxa and " << nsites << " site patterns";
        }
    }
#ifdef HAVE_NCL
    else {
        stateCount = 4;
        ncl_readAlignmentDNA(alignmentdna, &ntaxa, &nsites, compress);
        compactTipCount = ntaxa;
        alignmentFromFile = true;
        free(alignmentdna);
    }
#endif //HAVE_NCL

    if (!benchmarklist)
        std::cout << " (" << nreps << " rep" << (nreps > 1 ? "s" : "");

    std::cout << (manualScaling ? ", manual scaling":(autoScaling ? ", auto scaling":(dynamicScaling ? ", dynamic scaling":"")));

    if (!benchmarklist)
        std::cout << ", random seed " << randomSeed << ")";

    std::cout << "\n\n";

    if (benchmarklist || multiRsrc) {
        rsrcCount =  rsrc.size() - 1;
        if (rsrcCount == 0) {
            rsrcList = NULL;
        } else {
            rsrcList  = &rsrc[1];
        }
    }

    BeagleResourceList* rl = beagleGetResourceList();

    if(rl != NULL){
        for(int i=0; i<rl->length; i++){
            if (rsrc.size() == 1 || std::find(rsrc.begin(), rsrc.end(), i)!=rsrc.end()) {
                runBeagle(i,
                          stateCount,
                          ntaxa,
                          nsites,
                          manualScaling,
                          autoScaling,
                          dynamicScaling,
                          rateCategoryCount,
                          nreps,
                          fullTiming,
                          requireDoublePrecision,
                          disableVector,
                          enableThreads,
                          compactTipCount,
                          randomSeed,
                          rescaleFrequency,
                          unrooted,
                          calcderivs,
                          logscalers,
                          eigenCount,
                          eigencomplex,
                          ievectrans,
                          setmatrix,
                          opencl,
                          partitions,
                          sitelikes,
                          newDataPerRep,
                          randomTree,
                          rerootTrees,
                          pectinate,
                          benchmarklist,
                          pllTest,
                          pllSiteRepeats,
                          pllOnly,
                          multiRsrc,
                          postorderTraversal,
                          newTreePerRep,
                          newParametersPerRep,
                          threadCount,
                          rsrcList,
                          rsrcCount,
                          alignmentFromFile,
                          treenewick,
                          clientThreadingEnabled);
            }
        }
    } else {
        abort("no BEAGLE resources found");
    }

//#ifdef _WIN32
//    std::cout << "\nPress ENTER to exit...\n";
//    fflush( stdout);
//    fflush( stderr);
//    getchar();
//#endif
}
