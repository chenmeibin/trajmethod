//
// Created by adrian on 27/03/17.
//

#ifndef GRACT_REPAIR_HPP
#define GRACT_REPAIR_HPP

#include <global_constants.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <sdsl/int_vector.hpp>
#include <include/irepair.h>
#include <vector>

namespace gract_ct {


    typedef struct {
        int left, right;
    } Tpair;

    class repair {

    public:
        Tpair* rules;
        uint lenR;
        int* c;
        uint lenC;
        int alpha;

    private:

        void read_rules_repair(const char *fileR) {
            FILE *Tf;
            struct stat s;

            if (stat(fileR, &s) != 0) {
                fprintf(stderr, "Error: cannot stat file %s\n", fileR);
                exit(1);
            }
            uint64_t n = s.st_size;
            Tf = fopen(fileR, "rb");
            if (Tf == NULL) {
                fprintf(stderr, "Error: cannot open file %s for reading\n", fileR);
                exit(1);
            }

            if (fread(&alpha, sizeof(int), 1, Tf) != 1) {
                fprintf(stderr, "Error: cannot read file %s\n", fileR);
                exit(1);
            }
            lenR = (n - sizeof(int)) / sizeof(Tpair);
            rules = (Tpair *) malloc(lenR * sizeof(Tpair));
            if (fread(rules, sizeof(Tpair), lenR, Tf) != lenR) {
                fprintf(stderr, "Error: cannot read file %s\n", fileR);
                exit(1);
            }
            fclose(Tf);
        }

       void read_c_repair(const char *fileC) {
            FILE *Cf;
            struct stat s;

            if (stat(fileC, &s) != 0) {
                fprintf(stderr, "Error: cannot stat file %s\n", fileC);
                exit(1);
            }
            lenC = s.st_size / sizeof(int);
            Cf = fopen(fileC, "rb");
            if (Cf == NULL) {
                fprintf(stderr, "Error: cannot open file %s for reading\n", fileC);
                exit(1);
            }
            c = (int *) malloc(lenC * sizeof(int));
            if (fread(c, sizeof(int), lenC, Cf) != lenC) {
                fprintf(stderr, "Error: cannot read file %s\n", fileC);
                exit(1);
            }
            fclose(Cf);
        }

        int* read_keys_repair(const char *fileC, uint &lenK) {
            FILE *Cf;
            struct stat s;

            if (stat(fileC, &s) != 0) {
                fprintf(stderr, "Error: cannot stat file %s\n", fileC);
                exit(1);
            }
            lenK = s.st_size / sizeof(int);
            Cf = fopen(fileC, "rb");
            if (Cf == NULL) {
                fprintf(stderr, "Error: cannot open file %s for reading\n", fileC);
                exit(1);
            }
            int* k = (int *) malloc(lenK * sizeof(int));
            if (fread(k, sizeof(int), lenK, Cf) != lenK) {
                fprintf(stderr, "Error: cannot read file %s\n", fileC);
                exit(1);
            }
            fclose(Cf);
            return k;
        }

        void write_keys(const char* file, std::vector<int>& keys){
            FILE* f = fopen (file,"wb");
            for(uint64_t i = 0; i<keys.size(); i++){
                int value = keys[i];
                fwrite(&value,1,sizeof(int),f);
            }
            fclose(f);
        }

        int64_t translate(const int value_to_translate, const int last, const std::vector<int64_t > &keys){
            if(value_to_translate >= alpha){
                return last + (value_to_translate-alpha+1);
            }else{
                return (int) keys[value_to_translate];
            }
        }

        int translate(const int value_to_translate, const int &last, const int* keys){
            if(value_to_translate >= alpha){
                return last + (value_to_translate-alpha+1);
            }else{
                return keys[value_to_translate];
            }
        }

    public:

        repair(){};

        void run(int* &repair_ints, uint64_t len, const std::vector<int64_t> &terminals, const int ram_mb){

            std::string repair_file = "repair";
            std::string fileR = repair_file + ".R";
            std::string fileC = repair_file + ".C";


            repair_gn::build_grammar_irepair(repair_ints, len, ram_mb, repair_file.c_str());

            read_rules_repair(fileR.c_str());
            read_c_repair(fileC.c_str());

            //Unmapping values

        }

        void clear(){
            free(rules);
            free(c);
            lenR = 0;
            lenC = 0;
            alpha = 0;
        }



    };


}

#endif //RLZ_REFERENCE_REPAIR_HPP
