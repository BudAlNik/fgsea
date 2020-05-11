#include "fgseaMultilevelSupplement.h"
#include "esCalculation.h"

double betaMeanLog(unsigned long a, unsigned long b) {
    return boost::math::digamma(a) - boost::math::digamma(b + 1);
}

pair<double, bool> calcLogCorrection(const vector<unsigned int> &probCorrector, long probCorrIndx,
                         const pair<unsigned int, unsigned int> posUnifScoreCount, unsigned int sampleSize){
    double result = 0.0;
    result -= betaMeanLog(posUnifScoreCount.first, posUnifScoreCount.second);

    unsigned long halfSize = (sampleSize + 1) / 2;
    unsigned long remainder = sampleSize - probCorrIndx % (halfSize);

    double condProb = betaMeanLog(probCorrector[probCorrIndx] + 1, remainder);
    result += condProb;

    if (exp(condProb) >= 0.5){
        return make_pair(result, true);
    }
    else{
        return make_pair(result, false);
    }
}

void fillRandomSample(set<int> &randomSample, mt19937 &gen,
                      const unsigned long ranksSize, const unsigned int pathwaySize) {
    randomSample.clear();
    uniform_int_distribution<> uid_n(0, static_cast<int>(ranksSize - 1));
    while (static_cast<int>(randomSample.size()) < pathwaySize) {
        randomSample.insert(uid_n(gen));
    }
}


EsRuler::EsRuler(const vector<double> &inpRanks, unsigned int inpSampleSize, unsigned int inpPathwaySize) :
    ranks(inpRanks), sampleSize(inpSampleSize), pathwaySize(inpPathwaySize) {
    posUnifScoreCount = make_pair(0, 0);
    currentSamples.resize(inpSampleSize);
}

EsRuler::~EsRuler() = default;

vector<int> chunkLastElement;
int chunksNumber;

void EsRuler::duplicateSamples() {
    /*
     * Removes samples with an enrichment score less than the median value and
     * replaces them with samples with an enrichment score greater than the median
     * value
    */
    vector<pair<double, int> > stats(sampleSize);
    vector<int> posEsIndxs;
    int totalPosEsCount = 0;

    for (int sampleId = 0; sampleId < sampleSize; sampleId++) {
        double sampleEsPos = calcPositiveES(ranks, currentSamples[sampleId]);
        double sampleEs = calcES(ranks, currentSamples[sampleId]);
        if (sampleEs > 0) {
            totalPosEsCount++;
            posEsIndxs.push_back(sampleId);
        }
        stats[sampleId] = make_pair(sampleEsPos, sampleId);
    }
    sort(stats.begin(), stats.end());
    for (int sampleId = 0; 2 * sampleId < sampleSize; sampleId++) {
        enrichmentScores.push_back(stats[sampleId].first);
        if (find(posEsIndxs.begin(), posEsIndxs.end(), stats[sampleId].second) != posEsIndxs.end()) {
            totalPosEsCount--;
        }
        probCorrector.push_back(totalPosEsCount);
    }

    vector<vector<int> > new_sets;
    for (int sampleId = 0; 2 * sampleId < sampleSize - 2; sampleId++) {
        for (int rep = 0; rep < 2; rep++) {
            new_sets.push_back(currentSamples[stats[sampleSize - 1 - sampleId].second]);
        }
    }
    new_sets.push_back(currentSamples[stats[sampleSize >> 1].second]);
    swap(currentSamples, new_sets);
}

#include <iostream>

void EsRuler::extend(double ES, int seed, double eps) {
    unsigned int posCount = 0;
    unsigned int totalCount = 0;
    mt19937 gen(seed);

    for (int sampleId = 0; sampleId < sampleSize; sampleId++) {
        set<int> randomSample;
        fillRandomSample(randomSample, gen, ranks.size(), pathwaySize);
        currentSamples[sampleId] = vector<int>(randomSample.begin(), randomSample.end());
        double currentES = calcES(ranks, currentSamples[sampleId]);
        while (currentES <= 0) {
            fillRandomSample(randomSample, gen, ranks.size(), pathwaySize);
            currentES = calcES(ranks, vector<int>(randomSample.begin(), randomSample.end()));
            totalCount++;
        }
        posCount++;
        totalCount++;
    }

    posUnifScoreCount = make_pair(posCount, totalCount);
    chunksNumber = max(1, (int) (0.5 * sqrt(pathwaySize)));
    chunkLastElement = vector<int>(chunksNumber);
    chunkLastElement[chunksNumber - 1] = ranks.size();
    vector<int> tmp(sampleSize);
    vector<vector<double>> chunkSum(sampleSize, vector<double>(chunksNumber));
    vector<vector<int>> chunkSize(sampleSize, vector<int>(chunksNumber));
    vector<vector<vector<int>>> currentSampleChunks(sampleSize, vector<vector<int>>(chunksNumber));

    duplicateSamples();
    while (ES > enrichmentScores.back()){
        for (int i = 0, pos = 0; i < chunksNumber - 1; ++i) {
            pos += (pathwaySize + i) / chunksNumber;
            for (int j = 0; j < sampleSize; ++j) {
                tmp[j] = currentSamples[j][pos];
            }
            nth_element(tmp.begin(), tmp.begin() + sampleSize / 2, tmp.end());
            chunkLastElement[i] = tmp[sampleSize / 2];
        }

        for (int i = 0; i < sampleSize; ++i) {
            fill(chunkSum[i].begin(), chunkSum[i].end(), 0.0);
            fill(chunkSize[i].begin(), chunkSize[i].end(), 0);
            int cnt = 0;
            currentSampleChunks[i][cnt].clear();
            for (int pos : currentSamples[i]) {
                while (chunkLastElement[cnt] <= pos) {
                    ++cnt;
                    currentSampleChunks[i][cnt].clear();
                }
                currentSampleChunks[i][cnt].push_back(pos);
                chunkSum[i][cnt] += ranks[pos];
                chunkSize[i][cnt]++;
            }
        }

        for (int moves = 0; moves < sampleSize * pathwaySize;) {
            for (int sampleId = 0; sampleId < sampleSize; sampleId++) {
                moves += perturbate(ranks, pathwaySize, currentSampleChunks[sampleId], chunkSum[sampleId], chunkSize[sampleId], enrichmentScores.back(), gen);
            }
        }

        for (int i = 0; i < sampleSize; ++i) {
            currentSamples[i].clear();
            for (int j = 0; j < chunksNumber; ++j) {
                for (int pos : currentSampleChunks[i][j]) {
                    currentSamples[i].push_back(pos);
                }
            }
        }

        duplicateSamples();
        if (eps != 0){
            unsigned long k = enrichmentScores.size() / ((sampleSize + 1) / 2);
            if (k > - log2(0.5 * eps * exp(betaMeanLog(posUnifScoreCount.first, posUnifScoreCount.second)))) {
                break;
            }
        }
    }
}


pair<double, bool> EsRuler::getPvalue(double ES, double eps, bool sign) {
    unsigned long halfSize = (sampleSize + 1) / 2;

    auto it = enrichmentScores.begin();
    if (ES >= enrichmentScores.back()){
        it = enrichmentScores.end() - 1;
    }
    else{
        it = lower_bound(enrichmentScores.begin(), enrichmentScores.end(), ES);
    }

    unsigned long indx = 0;
    (it - enrichmentScores.begin()) > 0 ? (indx = (it - enrichmentScores.begin())) : indx = 0;

    unsigned long k = (indx) / halfSize;
    unsigned long remainder = sampleSize -  (indx % halfSize);

    double adjLog = betaMeanLog(halfSize, sampleSize);
    double adjLogPval = k * adjLog + betaMeanLog(remainder + 1, sampleSize);

    if (sign) {
        return make_pair(max(0.0, min(1.0, exp(adjLogPval))), true);
    } else {
        pair<double, bool> correction = calcLogCorrection(probCorrector, indx, posUnifScoreCount, sampleSize);
        double resLog = adjLogPval + correction.first;
        return make_pair(max(0.0, min(1.0, exp(resLog))), correction.second);
    }
}

#define szof(x) ((int) (x).size())

int perturbate(const vector<double> &ranks, int k, vector<vector<int>> &sampleChunks, vector<double> &chunkSum, vector<int> &chunkSize,
               double bound, mt19937 &rng) {
    double pertPrmtr = 0.1;
    int n = (int) ranks.size();
    // int k = (int) sample.size();
    uniform_int_distribution<> uid_n(0, n - 1);
    uniform_int_distribution<> uid_k(0, k - 1);
    double NS = 0;
    for (int i = 0; i < szof(sampleChunks); ++i) {
        for (int pos : sampleChunks[i]) {
            NS += ranks[pos];
        }
    }
    double q1 = 1.0 / (n - k);
    int iters = max(1, (int) (k * pertPrmtr));
    int moves = 0;
    for (int i = 0; i < iters; i++) {
        int oldInd = uid_k(rng);
        int oldChunkInd = 0, oldIndInChunk = 0;
        int oldVal;
        {
            int tmp = oldInd;
            while (szof(sampleChunks[oldChunkInd]) <= tmp) {
                tmp -= szof(sampleChunks[oldChunkInd]);
                ++oldChunkInd;
            }
            oldIndInChunk = tmp;
            oldVal = sampleChunks[oldChunkInd][oldIndInChunk];
        }

        int newVal = uid_n(rng);

        int newChunkInd = upper_bound(chunkLastElement.begin(), chunkLastElement.end(), newVal) - chunkLastElement.begin();
        int newIndInChunk = lower_bound(sampleChunks[newChunkInd].begin(), sampleChunks[newChunkInd].end(), newVal) - sampleChunks[newChunkInd].begin();

        if (newIndInChunk < szof(sampleChunks[newChunkInd]) && sampleChunks[newChunkInd][newIndInChunk] == newVal) {
            if (newVal == oldVal) {
                ++moves;
            }
            continue;
        }

        sampleChunks[oldChunkInd].erase(sampleChunks[oldChunkInd].begin() + oldIndInChunk);
        sampleChunks[newChunkInd].insert(
            sampleChunks[newChunkInd].begin() + newIndInChunk - (oldChunkInd == newChunkInd && oldIndInChunk < newIndInChunk ? 1 : 0), 
            newVal);

        NS = NS - ranks[oldVal] + ranks[newVal];

        chunkSum[oldChunkInd] -= ranks[oldVal];
        chunkSize[oldChunkInd]--;

        chunkSum[newChunkInd] += ranks[newVal];
        chunkSize[newChunkInd]++;

        double cur = 0.0;
        double q2 = 1.0 / NS;
        int last = -1;
        int cnt = 0;
        int lastChunkLastElement = 0;

        bool ok = false;

        for (int i = 0; i < chunksNumber; ++i) {
            if (cur + q2 * chunkSum[i] < bound) {
                cur = cur + q2 * chunkSum[i] - q1 * (chunkLastElement[i] - lastChunkLastElement - chunkSize[i]);
                last = chunkLastElement[i] - 1;
                cnt += chunkSize[i];
            } else {
                for (int pos : sampleChunks[i]) {
                    cur += q2 * ranks[pos] - q1 * (pos - last - 1);
                    if (cur > bound) {
                        ok = true;
                        break;
                    }
                    last = pos;
                    ++cnt;
                }
                if (ok) {
                    break;
                }
            }
            lastChunkLastElement = chunkLastElement[i];
        }

        if (!ok) {
            NS = NS - ranks[newVal] + ranks[oldVal];
            
            chunkSum[oldChunkInd] += ranks[oldVal];
            chunkSize[oldChunkInd]++;

            chunkSum[newChunkInd] -= ranks[newVal];
            chunkSize[newChunkInd]--;

            sampleChunks[newChunkInd].erase(
                sampleChunks[newChunkInd].begin() + newIndInChunk - (oldChunkInd == newChunkInd && oldIndInChunk < newIndInChunk ? 1 : 0));
            sampleChunks[oldChunkInd].insert(sampleChunks[oldChunkInd].begin() + oldIndInChunk, oldVal);
        } else {
            ++moves;
        }
    }
    return moves;
}