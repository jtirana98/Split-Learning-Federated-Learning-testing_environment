#include <torch/torch.h>
#include <torch/data/datasets/base.h>
#include <torch/data/example.h>
#include <torch/types.h>
#include <iostream>
#include <vector>
#include <string>

#include "models.h"


int main(int argc, char **argv) {
    std::vector<int> batches{32, 64, 128};
    std::vector<int> splits{2, 4, 6, 8};
    
  //train_resnet(CIFAR_10, resnet101, false, 128, std::vector<int>(), false);
    
  // train_resnet(CIFAR_10, resnet101, true, 128);

  train_vgg(CIFAR_10, v19, false, 128, std::vector<int>(), false);
  //train_vgg(CIFAR_10, v19, true, 128);
} 
