#include <stdlib.h>
#include <superneurons.h>
using namespace SuperNeurons;

int main(int argc, char **argv) {
    char* train_label_bin;
    char* train_image_bin;
    char* test_label_bin;
    char* test_image_bin;

    train_image_bin = (char *) "/home/ay27/data/mnist/mnist_train_image.bin";
    train_label_bin = (char *) "/home/ay27/data/mnist/mnist_train_label.bin";
    test_image_bin  = (char *) "/home/ay27/data/mnist/mnist_test_image.bin";
    test_label_bin  = (char *) "/home/ay27/data/mnist/mnist_test_label.bin";

    const size_t batch_size = 100; //train and test must be same
    //train
    parallel_reader_t<float> reader1(train_image_bin, train_label_bin, 2, batch_size, 1, 28, 28);
    base_layer_t<float>* data_1 = (base_layer_t<float>*) new data_layer_t<float>(DATA_TRAIN, &reader1);

    