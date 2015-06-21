#include <vector>
#include <boost/algorithm/string.hpp>
#include "JoinScore.h"
#include "moses/ScoreComponentCollection.h"
#include "moses/Hypothesis.h"
#include "moses/InputFileStream.h"

using namespace std;

namespace Moses
{
////////////////////////////////////////////////////////////////
int JoinScore::JoinScoreState::Compare(const FFState& other) const
{
  const JoinScoreState &otherState = static_cast<const JoinScoreState&>(other);

  if (m_morphemes == otherState.m_morphemes)
    return 0;
  return (m_morphemes < otherState.m_morphemes) ? -1 : +1;
}
////////////////////////////////////////////////////////////////
JoinScore::Node::Node()
:isAWord(false)
{  
}

JoinScore::Node *JoinScore::Node::Insert(const std::string &tok)
{
  return Insert(tok, 0);
}

JoinScore::Node *JoinScore::Node::Insert(const std::string &tok, size_t pos)
{
  if (pos == tok.size()) {
    this->isAWord = true;
    return this;
  }
  else {
    char c = tok[pos];
    
    Node *child = GetOrCreateNode(c);
    return child->Insert(tok, pos + 1);
  }
}

JoinScore::Node *JoinScore::Node::GetOrCreateNode(char c)
{
	Children::iterator iter;
	iter = m_children.find(c);
	if (iter == m_children.end()) {
		 Node *node = new Node();
    m_children[c] = node;
    cerr << c << " " << node << endl;
    return node;
	}
	else {
		Node *child = iter->second;
		assert(child);
		return child;
	}  

}

const JoinScore::Node *JoinScore::Node::Find(const std::string &tok) const
{
  const Node *ret = Find(tok, 0);
  return ret;
}

const JoinScore::Node *JoinScore::Node::Find(const std::string &tok, size_t pos) const
{
  if (pos == tok.size()) {
    //cerr << " ret=" << this;
    return this;
  }
  else {
    char c = tok[pos];
    
    Children::const_iterator iter;
    iter = m_children.find(c);
    if (iter == m_children.end()) {
      //cerr << " " << c << "=NULL";
      return NULL;
    }
    else {
      const Node *child = iter->second;
      //cerr << " " << c << "=" << child;
      assert(child);
      return child->Find(tok, pos + 1);
    }
  }
}

////////////////////////////////////////////////////////////////
JoinScore::JoinScore(const std::string &line)
  :StatefulFeatureFunction(4, line)
  ,m_scoreRealWords(true)
  ,m_scoreNumCompounds(true) 
  ,m_scoreInvalidJoins(true)
  ,m_scoreCompoundWord(true)
  ,m_maxMorphemeState(-1)
  ,m_multiplier(1)
{
  ReadParameters();
  
  UTIL_THROW_IF2(m_scoreCompoundWord && m_vocabPath.empty(), "Must provide path to vocab file");
}

void JoinScore::Load()
{
  if (!m_vocabPath.empty()) {
    InputFileStream vocabStrme(m_vocabPath);

    string line;
    while (getline(vocabStrme, line)) {
      vector<string> toks;
      Tokenize(toks, line);
      for (size_t i = 0; i < toks.size(); ++i) {
        const string &tok = toks[i];
        m_vocabRoot.Insert(tok);
      }
    }
  }
}

void JoinScore::EvaluateInIsolation(const Phrase &source
    , const TargetPhrase &targetPhrase
    , ScoreComponentCollection &scoreBreakdown
    , ScoreComponentCollection &estimatedFutureScore) const
{}

void JoinScore::EvaluateWithSourceContext(const InputType &input
    , const InputPath &inputPath
    , const TargetPhrase &targetPhrase
    , const StackVec *stackVec
    , ScoreComponentCollection &scoreBreakdown
    , ScoreComponentCollection *estimatedFutureScore) const
{}

void JoinScore::EvaluateTranslationOptionListWithSourceContext(const InputType &input
    , const TranslationOptionList &translationOptionList) const
{}

FFState* JoinScore::EvaluateWhenApplied(
  const Hypothesis& cur_hypo,
  const FFState* prev_state,
  ScoreComponentCollection* accumulator) const
{
  const JoinScoreState *classState = static_cast<const JoinScoreState*>(prev_state);
  int prevJuncture = classState->GetJuncture();
  Phrase morphemes = classState->GetMorphemes();
  const Node *prevNode = classState->GetNode();
  
  size_t countWord = 0;
  size_t countCompound = 0;
  size_t countInvalidJoin = 0;
  float compoundWordScore = 0;

  const Phrase &tp = cur_hypo.GetCurrTargetPhrase();
  for (size_t pos = 0; pos < tp.GetSize(); ++pos) {
    const Word &word = tp.GetWord(pos);
    int currJuncture = GetJuncture(word);
    
    CalcScores(countWord, countCompound, 
              countInvalidJoin, compoundWordScore, 
              morphemes, prevNode, &word, 
              prevJuncture, currJuncture);

    if (morphemes.GetSize()) {
      if (currJuncture == 2 || currJuncture == 3) {
        float score = CalcMorphemeScore(prevNode, morphemes, false);
        compoundWordScore += score;
      }
    }
    else {
        // whole world or end of compound.
        prevNode = NULL;
    }
    
    prevJuncture = currJuncture;
  }

  // end of sentence
  if (cur_hypo.IsSourceCompleted()) {
    int currJuncture = 0;
    CalcScores(countWord, countCompound, countInvalidJoin, compoundWordScore, 
              morphemes, prevNode, NULL, 
              prevJuncture, currJuncture);  
    //cerr << morphemes.ToString() << "=" << prevNode << endl;            
  }

  // set scores
  vector<float> scores(m_numScoreComponents, 0);
  size_t ind = 0;
  if (m_scoreRealWords) {
    scores[ind++] = CalcScore(countWord);
  }
  if (m_scoreNumCompounds) {
    scores[ind++] = CalcScore(countCompound);
  }
  if (m_scoreInvalidJoins) {
    scores[ind++] = CalcScore(countInvalidJoin);    
  }
  if (m_scoreCompoundWord) {
    scores[ind++] = CalcScore(compoundWordScore);
  }
  UTIL_THROW_IF2(ind > m_numScoreComponents, "Vector element out-of-range:" << ind << ">" << m_numScoreComponents);
  
  //cerr << "score=" << scores[0] << endl;
  accumulator->PlusEquals(this, scores);
  
  return new JoinScoreState(morphemes, prevJuncture, prevNode);
}

float JoinScore::CalcScore(float count) const
{
  float ret = m_tuneable ? count * m_multiplier
                        : (count?-std::numeric_limits<float>::infinity():0);
  return ret;
}

void JoinScore::CalcScores(size_t &countWord, size_t&countCompound, 
                          size_t &countInvalidJoin, float &compoundWordScore, 
                          Phrase &morphemes, const Node *&node, 
                          const Word *morpheme,
                          int prevJuncture, int currJuncture) const
{
  if (prevJuncture < 0 || prevJuncture > 4 || currJuncture < 0 || currJuncture > 4) {
      UTIL_THROW2("Invalid juncture value: " << prevJuncture << " " << currJuncture);
  }
  
  switch (prevJuncture) {
    case 0:
      switch (currJuncture) {
        case 0:
          ++countWord;
          break;
        case 1:
          ++countInvalidJoin;
          ++countWord;
          ++countCompound;
          
          assert(morphemes.GetSize() == 0);
          AddMorphemeToState(morphemes, morpheme);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          break;
        case 2:
          ++countWord;
          ++countCompound;
          
          assert(morphemes.GetSize() == 0);
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 3:
          ++countInvalidJoin;
          ++countWord;
          ++countCompound;

          assert(morphemes.GetSize() == 0);
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 4:
          ++countWord;
          break;
      }
      break;
    case 1:
      switch (currJuncture) {
        case 0:
          ++countWord;
          break;
        case 1:
          ++countInvalidJoin;
          ++countWord;
          ++countCompound;
          
          assert(morphemes.GetSize() == 0);
          AddMorphemeToState(morphemes, morpheme);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          break;
        case 2:
          ++countWord;
          ++countCompound;
          
          assert(morphemes.GetSize() == 0);
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 3:
          ++countInvalidJoin;
          ++countWord;
          ++countCompound;

          assert(morphemes.GetSize() == 0);
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 4:
          ++countWord;
          break;
      }
      break;
    case 2:
      switch (currJuncture) {
        case 0:
          ++countInvalidJoin;
          ++countWord;
          
          assert(morphemes.GetSize() || m_maxMorphemeState == 0);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          break;
        case 1:
          assert(morphemes.GetSize() || m_maxMorphemeState == 0);
          AddMorphemeToState(morphemes, morpheme);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          break;
        case 2:
          ++countInvalidJoin;
          ++countWord;
          ++countCompound;

          assert(morphemes.GetSize() || m_maxMorphemeState == 0);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 3:
          assert(morphemes.GetSize() || m_maxMorphemeState == 0);
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 4:
          break;
      }
      break;
    case 3:
      switch (currJuncture) {
        case 0:
          ++countInvalidJoin;
          ++countWord;

          assert(morphemes.GetSize() || m_maxMorphemeState == 0);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          break;
        case 1:
          assert(morphemes.GetSize() || m_maxMorphemeState == 0);
          AddMorphemeToState(morphemes, morpheme);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          break;
        case 2:
          ++countInvalidJoin;
          ++countWord;
          ++countCompound;

          assert(morphemes.GetSize() || m_maxMorphemeState == 0);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 3:
          assert(morphemes.GetSize() || m_maxMorphemeState == 0);
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 4:
          break;
      }
      break;
    case 4:
      switch (currJuncture) {
        case 0:
          ++countWord;

          assert(morphemes.GetSize());
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          break;
        case 1:
          assert(morphemes.GetSize());
          AddMorphemeToState(morphemes, morpheme);
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          break;
        case 2:
          ++countWord;
          ++countCompound;

          assert(morphemes.GetSize());
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          AddMorphemeToState(morphemes, morpheme);
          break;
        case 3:
          break;
        case 4:
          ++countWord;

          assert(morphemes.GetSize());
          compoundWordScore += CalcMorphemeScore(node, morphemes, true);
          morphemes.Clear();
          AddMorphemeToState(morphemes, morpheme);
          break;
      }
      break;
  }
}

FFState* JoinScore::EvaluateWhenApplied(
  const ChartHypothesis& /* cur_hypo */,
  int /* featureID - used to index the state in the previous hypotheses */,
  ScoreComponentCollection* accumulator) const
{
  return new JoinScoreState();
}

void JoinScore::SetParameter(const std::string& key, const std::string& value)
{
  if (key == "score-real-words") {
    m_scoreRealWords = Scan<bool>(value);
  }
  else if (key == "score-num-compounds") {
    m_scoreNumCompounds = Scan<bool>(value);  
  }
  else if (key == "score-invalid-joins") {
    m_scoreInvalidJoins = Scan<bool>(value);      
  }
  else if (key == "score-compound-word") {
    m_scoreCompoundWord = Scan<bool>(value);  
  }
  else if (key == "score-compound-oov") {
    m_scoreCompoundOOV = Scan<bool>(value);  
  }
  else if (key == "max-morpheme-state") {
    m_maxMorphemeState = Scan<int>(value);  
  } 
  else if (key == "multiplier") {
    m_multiplier = Scan<float>(value);  
  } 
  else if (key == "vocab-path") {
    m_vocabPath = value;
  }
  else {
    StatefulFeatureFunction::SetParameter(key, value);
  }
}

int JoinScore::GetJuncture(const Word &morpheme) const
{  
  if (morpheme.IsOOV()) {
      return 4;
  }
  
 int ret = 0;

 const Factor *factor = morpheme.GetFactor(0);
  StringPiece str = factor->GetString();
  if (str.size() > 1) {
    if (str.starts_with("+")) {
      ret += 1;
    }
    if (str.ends_with("+")) {
      ret += 2;
    }
  }
  
  return ret;
}

float JoinScore::CalcMorphemeScore(const Node *&node, const Phrase &morphemes, bool wholeWord) const
{
  string wordStr = morphemes.ToString();
  wordStr = Trim(wordStr);
  boost::replace_all(wordStr, "+ +", "");
  boost::replace_all(wordStr, "+", "");
  
  node = m_vocabRoot.Find(wordStr);
  //cerr << wordStr << "=" << node << endl;
  
  float ret;
  if (node) {
    if (wholeWord) {
      bool isAWord = node->isAWord;
      ret = isAWord ? 0 : 1;
    }
    else {
      ret = 0;
    }
    
  }
  else {
    ret = 1;
  }
  //cerr << "==" << ret << endl;
  
  return ret;
}

void JoinScore::AddMorphemeToState(Phrase &morphemes, const Word *morpheme) const
{
  if (m_maxMorphemeState < 0) {
    // unlimited number of morph. Don't delete any prev morph
  }
  else if (m_maxMorphemeState == 0) {
    // stateless. Don't add any morphemes to state
    return;
  }
  else if (morphemes.GetSize() == m_maxMorphemeState) {
    morphemes.RemoveWord(0);
  }
  else if (morphemes.GetSize() < m_maxMorphemeState) {
    // do nothing
  }  
  else {
    UTIL_THROW2("Number of morphemes (" << morphemes.GetSize() 
              << " exceed max (" << m_maxMorphemeState << ")");
  }
  
  assert(morpheme);
  morphemes.AddWord(*morpheme);
}

}
