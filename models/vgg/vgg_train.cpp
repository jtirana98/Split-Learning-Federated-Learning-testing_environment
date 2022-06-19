
#include "vgg_train.h"

VGG getModel(vgg_model model_option, int num_classes) {
    switch (model_option) {
    case v11:
        return vgg11(num_classes);
        break;
    case v11_bn:
        return vgg11_bn(num_classes);
        break;
    case v13:
        return vgg13(num_classes);
        break;
    case v13_bn:
        return vgg13_bn(num_classes);
        break;
    case v16:
        return vgg16(num_classes);
        break;
    case v16_bn:
        return vgg16_bn(num_classes);
        break;
    case v19:
        return vgg19(num_classes);
        break;
    case v19_bn:
        return vgg19_bn(num_classes);
    default:
        break;
    }
}

std::vector<torch::nn::Sequential> getSplitModel(vgg_model model_option, int num_classes, std::vector<int> split_points) {
    switch (model_option) {
    case v11:
        return vgg11_split(num_classes, split_points);
        break;
    case v13:
        return vgg13_split(num_classes, split_points);
        break;
    case v16:
        return vgg16_split(num_classes, split_points);
        break;
    case v19:
        return vgg19_split(num_classes, split_points);
    default:
        break;
    }
}

void printModelsParameters(VGG& model) {
    for (const auto& p : model->parameters()) {
        int dims = p.dim();
        std::cout << "=";
        for (int i=0; i < dims; i++) {
            //std::cout << i << ": " << p.size(i) << "    ";
            std::cout << p.size(i);
            if (i!=dims-1) 
                std::cout << "*";
        }

        std::cout << "\t";
        
    }
    std::cout << std::endl;
}

// MNIST
void vgg_mnist(vgg_model model_option) {
    std::vector<gatherd_data> all_measures;

    auto train_dataset = torch::data::datasets::MNIST(MNIST_data_path)
                            .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                            .map(torch::data::transforms::Stack<>());
    auto num_train_samples = train_dataset.size().value();

    auto train_dataloader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
                std::move(train_dataset), batch_size);

    int num_classes = 10;
    auto model = getModel(model_option, num_classes);

    printModelsParameters(model);

    // Initilize optimizer
    double weight_decay = 0.0001;  // regularization parameter
    torch::optim::Adam optimizer(model->parameters(), torch::optim::AdamOptions(learning_rate));

    Total totaltimes = Total();

    for (size_t epoch = 0; epoch != num_epochs; ++epoch) {
        // Initialize running metrics
        double running_loss = 0.0;
        size_t num_correct = 0;

        int batch_index = 0;
        for (auto& batch : *train_dataloader) {
            optimizer.zero_grad();

            // Transfer images and target labels to device
            auto data = batch.data;
            auto target = batch.target;

            // Forward
            Event start_forward(forward, "", -1);
            torch::Tensor output = model->forward(batch.data);
            
            torch::Tensor loss =
                   torch::nn::functional::cross_entropy(output, batch.target);

            running_loss += loss.item<double>() * data.size(0);

            auto prediction = output.argmax(1);
            num_correct += prediction.eq(target).sum().item<int64_t>();

            Event start_backprop(backprop, "", -1);
            loss.backward();
            optimizer.step();
            Event end_batch(backprop, "", -1);

            totaltimes.addNew(start_forward, start_backprop, end_batch);

            if (++batch_index % 15 == 0) {
                /*
                std::cout << "Epoch: " << epoch << " | Batch: " << batch_index
                          << " | Loss: " << loss.item<float>() << std::endl;
                */
                //if (epoch == 0) {
                    totaltimes.printRes();
                    break;
                //}
            }
        }
    }

}

void vgg_split_mnist(vgg_model model_option, const std::vector<int>& split_points = std::vector<int>()) {
    std::vector<gatherd_data> all_measures;

    auto train_dataset = torch::data::datasets::MNIST(MNIST_data_path)
                            .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                            .map(torch::data::transforms::Stack<>());
    auto num_train_samples = train_dataset.size().value();

    auto train_dataloader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
                std::move(train_dataset), batch_size);
    
    int num_classes = 10;
    auto layers = getSplitModel(model_option, num_classes, split_points);

    std::vector<torch::optim::Adam> optimizers;
    
    for (int i=0; i<layers.size(); i++) {
        optimizers.push_back(torch::optim::Adam(layers[i]->parameters(), torch::optim::AdamOptions(learning_rate)));
        //std::cout << layers[i] << std::endl;
    }

    gatherd_data data_loads;
    Total totaltimes = Total();

    for (size_t epoch = 0; epoch != num_epochs; ++epoch) {
        // Initialize running metrics
        double running_loss = 0.0;
        size_t num_correct = 0;

        int batch_index = 0;
        for (auto& batch : *train_dataloader) {
            for (int i=0; i<optimizers.size(); i++) {
                optimizers[i].zero_grad();
            }
            
            std::vector<torch::Tensor> outputs, detached_outputs;

            // Transfer images and target labels to device
            auto data = batch.data;
            auto target = batch.target;

            torch::Tensor prev_out = data;
            int k = 0;
            
            for (torch::nn::Sequential layer : layers) {
                if (batch_index != 0)
                    totaltimes.addEvent(Event(measure_type::forward, "", k));

                if (batch_index == 0) {
                    //std::cout << prev_out.sizes() << "\t";
                    std::stringstream data_load;
                    torch::save(prev_out, data_load);
                    data_loads.activations.push_back(
                        dataload{k, measure_type::activations_load, data_load.tellp()});
                }

                auto output = layer->forward(prev_out);

                if (k == avg_point) {
                    
                    //if (batch_index == 0)
                    //    std::cout << "-"<< output.sizes() << "\t";
                    output = output.view({data.size(0), -1});
                }

                if (k != layers.size()-1) {
                    auto output_detached = output.clone().detach().requires_grad_(true);
                    detached_outputs.push_back(output_detached);
                    prev_out = output_detached;
                    outputs.push_back(output);
                }
                else {
                    prev_out = output;
                }

                k += 1;
            }
                
            if (batch_index == 0) {
                //std::cout << prev_out.sizes() << "\t";
                std::stringstream data_load;
                torch::save(prev_out, data_load);
                //std::cout << data_load.tellp() << "\t";
                data_loads.activations.push_back(
                    dataload{k, measure_type::activations_load, data_load.tellp()});
                //std::cout << std::endl;
            }
            
        
            if (batch_index != 0)
                totaltimes.addEvent(Event(backprop, "", layers.size()-1));
            torch::Tensor loss =
                   torch::nn::functional::cross_entropy(prev_out, batch.target);

            running_loss += loss.item<double>() * data.size(0);

            auto prediction = prev_out.argmax(1);
            num_correct += prediction.eq(target).sum().item<int64_t>();
            loss.backward();
            optimizers[optimizers.size()-1].step();
            
            
            for (int i = 0; i< detached_outputs.size(); i++) {
                if (batch_index != 0)
                    totaltimes.addEvent(Event(backprop, "", i));
                auto prev_grad = detached_outputs[detached_outputs.size()-1-i].grad().clone().detach();
                
                
                if (batch_index == 0) {
                    std::stringstream data_load;
                    torch::save(prev_grad, data_load);
                    data_loads.gradients.push_back(
                        dataload{k, measure_type::gradients_load, data_load.tellp()});
                }
            
                
                outputs[outputs.size() - 1 - i].backward(prev_grad);
                optimizers[optimizers.size() - 2 - i].step();
            }

            if (batch_index != 0) {
                totaltimes.addEvent(Event(backprop, "end", -1));
                totaltimes.computeIntervals();
            }

            if (++batch_index % 16 == 0) {
                //std::cout << "Epoch: " << epoch << " | Batch: " << batch_index
                //          << " | Loss: " << loss.item<float>() << std::endl;
                
                //if (epoch == 0) {
                    totaltimes.printRes_intervals();
                    break;
                //}
            }
        }

    }

    std::cout << "activations load" << std::endl;
    for (int i = 0; i<data_loads.activations.size(); i++) {
        std::cout << data_loads.activations[i].data_load << "\t";
    }

    std::cout << std::endl << "gradients load" << std::endl;
    for (int i = data_loads.gradients.size() - 1; i>0; i--) {
        std::cout << data_loads.gradients[i].data_load << "\t";
    }


}

// CIFAR
void vgg_cifar(vgg_model model_option, int type) {
    std::vector<gatherd_data> all_measures;

    auto path_selection = (type == 1)? CIFAR10_data_path : CIFAR100_data_path;

    auto train_dataset = CIFAR(path_selection, type)
                                    .map(ConstantPad(4))
                                    .map(RandomHorizontalFlip())
                                    .map(RandomCrop({32, 32}))
                                    .map(torch::data::transforms::Stack<>());
    auto num_train_samples = train_dataset.size().value();

    auto train_dataloader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
            std::move(train_dataset), batch_size);

    int num_classes = (type == 1)? 10 : 100;
    auto model = getModel(model_option, num_classes);

    printModelsParameters(model);
    
    // Initilize optimizer
    double weight_decay = 0.0001;  // regularization parameter
    torch::optim::Adam optimizer(model->parameters(), torch::optim::AdamOptions(learning_rate));

    Total totaltimes = Total();

    for (size_t epoch = 0; epoch != num_epochs; ++epoch) {
        // Initialize running metrics
        double running_loss = 0.0;
        size_t num_correct = 0;

        int batch_index = 0;
        for (auto& batch : *train_dataloader) {
            optimizer.zero_grad();

            // Transfer images and target labels to device
            auto data = batch.data;
            auto target = batch.target;

            // Forward
            Event start_forward(forward, "", -1);
            torch::Tensor output = model->forward(batch.data);
            
            torch::Tensor loss =
                   torch::nn::functional::cross_entropy(output, batch.target);

            running_loss += loss.item<double>() * data.size(0);

            auto prediction = output.argmax(1);
            num_correct += prediction.eq(target).sum().item<int64_t>();

            Event start_backprop(backprop, "", -1);
            loss.backward();
            optimizer.step();
            Event end_batch(backprop, "", -1);

            totaltimes.addNew(start_forward, start_backprop, end_batch);

            if (++batch_index % 15 == 0) {
                /*
                std::cout << "Epoch: " << epoch << " | Batch: " << batch_index
                          << " | Loss: " << loss.item<float>() << std::endl;
                */
                //if (epoch == 0) {
                    totaltimes.printRes();
                    break;
                //}
            }
        }

        /*
        auto sample_mean_loss = running_loss / dataloader.num_train_samples;
        auto accuracy = static_cast<double>(num_correct) / dataloader.num_train_samples;

        
        std::cout << "Epoch [" << (epoch + 1) << "/" << num_epochs << "], Trainset - Loss: "
            << sample_mean_loss << ", Accuracy: " << accuracy << '\n';
        */

    }


}

void vgg_split_cifar(vgg_model model_option, int type, const std::vector<int>& split_points = std::vector<int>()) {
    std::vector<gatherd_data> all_measures;

    auto path_selection = (type == 1)? CIFAR10_data_path : CIFAR100_data_path;
    auto train_dataset = CIFAR(path_selection, type)
                                    .map(ConstantPad(4))
                                    .map(RandomHorizontalFlip())
                                    .map(RandomCrop({32, 32}))
                                    .map(torch::data::transforms::Stack<>());
    auto num_train_samples = train_dataset.size().value();

    auto train_dataloader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
            std::move(train_dataset), batch_size);
    
    int num_classes = (type == 1)? 10 : 100;
    auto layers = getSplitModel(model_option, num_classes, split_points);

    std::vector<torch::optim::Adam> optimizers;
    
    for (int i=0; i<layers.size(); i++) {
        optimizers.push_back(torch::optim::Adam(layers[i]->parameters(), torch::optim::AdamOptions(learning_rate)));
        std::cout << "new layer: " << layers[i] << std::endl;
    }

    gatherd_data data_loads;
    Total totaltimes = Total();

    int avg_point;
    int reverse_count = 0;
    for (int i=layers.size()-1; i>=0; i--) {
        reverse_count = reverse_count + layers[i]->size();
        if (reverse_count > 7) {
            avg_point = i;
            break;
        }
    }

    for (size_t epoch = 0; epoch != num_epochs; ++epoch) {
        // Initialize running metrics
        double running_loss = 0.0;
        size_t num_correct = 0;

        int batch_index = 0;
        for (auto& batch : *train_dataloader) {
            for (int i=0; i<optimizers.size(); i++) {
                optimizers[i].zero_grad();
            }
            
            std::vector<torch::Tensor> outputs, detached_outputs;

            // Transfer images and target labels to device
            auto data = batch.data;
            auto target = batch.target;

            torch::Tensor prev_out = data;
            int k = 0;
            
            for (torch::nn::Sequential layer : layers) {
                if (batch_index != 0)
                    totaltimes.addEvent(Event(measure_type::forward, "", k));

                if (batch_index == 0) {
                    //std::cout << prev_out.sizes() << "\t";
                    std::stringstream data_load;
                    torch::save(prev_out, data_load);
                    data_loads.activations.push_back(
                        dataload{k, measure_type::activations_load, data_load.tellp()});
                }

                auto output = layer->forward(prev_out);

                if (k == avg_point) {
                    
                    //if (batch_index == 0)
                    //    std::cout << "-"<< output.sizes() << "\t";
                    output = output.view({data.size(0), -1});
                }

                if (k != layers.size()-1) {
                    auto output_detached = output.clone().detach().requires_grad_(true);
                    detached_outputs.push_back(output_detached);
                    prev_out = output_detached;
                    outputs.push_back(output);
                }
                else {
                    prev_out = output;
                }

                k += 1;
            }
                
            if (batch_index == 0) {
                //std::cout << prev_out.sizes() << "\t";
                std::stringstream data_load;
                torch::save(prev_out, data_load);
                //std::cout << data_load.tellp() << "\t";
                data_loads.activations.push_back(
                    dataload{k, measure_type::activations_load, data_load.tellp()});
                //std::cout << std::endl;
            }
            
        
            if (batch_index != 0)
                totaltimes.addEvent(Event(backprop, "", layers.size()-1));
            torch::Tensor loss =
                   torch::nn::functional::cross_entropy(prev_out, batch.target);

            running_loss += loss.item<double>() * data.size(0);

            auto prediction = prev_out.argmax(1);
            num_correct += prediction.eq(target).sum().item<int64_t>();
            loss.backward();
            optimizers[optimizers.size()-1].step();
            
            
            for (int i = 0; i< detached_outputs.size(); i++) {
                if (batch_index != 0)
                    totaltimes.addEvent(Event(backprop, "", i));
                auto prev_grad = detached_outputs[detached_outputs.size()-1-i].grad().clone().detach();
                
                
                if (batch_index == 0) {
                    std::stringstream data_load;
                    torch::save(prev_grad, data_load);
                    data_loads.gradients.push_back(
                        dataload{k, measure_type::gradients_load, data_load.tellp()});
                }
            
                
                outputs[outputs.size() - 1 - i].backward(prev_grad);
                optimizers[optimizers.size() - 2 - i].step();
            }

            if (batch_index != 0) {
                totaltimes.addEvent(Event(backprop, "end", -1));
                totaltimes.computeIntervals();
            }

            if (++batch_index % 16 == 0) {
                //std::cout << "Epoch: " << epoch << " | Batch: " << batch_index
                //          << " | Loss: " << loss.item<float>() << std::endl;
                
                //if (epoch == 0) {
                    totaltimes.printRes_intervals();
                    break;
                //}
            }
        }

        /*
        auto sample_mean_loss = running_loss / dataloader.num_train_samples;
        auto accuracy = static_cast<double>(num_correct) / dataloader.num_train_samples;

        std::cout << "Epoch [" << (epoch + 1) << "/" << num_epochs << "], Trainset - Loss: "
            << sample_mean_loss << ", Accuracy: " << accuracy << '\n';
        */
    }


    std::cout << "activations load" << std::endl;
    for (int i = 0; i<data_loads.activations.size(); i++) {
        std::cout << data_loads.activations[i].data_load << "\t";
    }

    std::cout << std::endl << "gradients load" << std::endl;
    for (int i = data_loads.gradients.size() - 1; i>0; i--) {
        std::cout << data_loads.gradients[i].data_load << "\t";
    }

}

void train_vgg(dataset dataset_option, vgg_model model_option, bool split, const std::vector<int>& split_points) {
    if (split) {
        switch (dataset_option) {
        case MNIST:
            vgg_split_mnist(model_option, split_points);
            break;
        case CIFAR_10:
            vgg_split_cifar(model_option, 1, split_points);
            break;
        case CIFAR_100:
            vgg_split_cifar(model_option, 0, split_points);
            break;
        default:
            break;
        }

    }
    else {
        switch (dataset_option) {
        case MNIST:
            vgg_mnist(model_option);
            break;
        case CIFAR_10:
            vgg_cifar(model_option, 1);
            break;
        case CIFAR_100:
            vgg_cifar(model_option, 0);
            break;
        default:
            break;
        }

    }
}