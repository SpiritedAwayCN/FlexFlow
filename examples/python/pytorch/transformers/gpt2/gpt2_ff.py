import os
import sys

import numpy as np
from flexflow.core import *
from flexflow.torch.model import PyTorchModel
from transformers import GPT2Config, GPT2ForSequenceClassification, GPT2Tokenizer
from datasets import load_dataset

config = GPT2Config.from_pretrained("distilgpt2")
model = GPT2ForSequenceClassification.from_pretrained("distilgpt2", config=config)
tokenizer = GPT2Tokenizer.from_pretrained("distilgpt2")
max_input_length = tokenizer.max_model_input_sizes['distilgpt2']

def prepare_numpy_dataset():
    dataset = load_dataset("imdb")
    train_dataset = dataset["train"].shuffle(seed=123)
    test_dataset = dataset["test"].shuffle(seed=123)
    
    if tokenizer.pad_token is None:
        tokenizer.add_special_tokens({'pad_token': '[PAD]'})

    encode_train_dataset = train_dataset.map(lambda examples: tokenizer(
        examples['text'], truncation=True, padding=True, max_length=max_input_length), batched=True)
    encode_test_dataset = test_dataset.map(lambda examples: tokenizer(
        examples['text'], truncation=True, padding=True, max_length=max_input_length), batched=True)
    
    #change format to numpy
    encode_train_dataset.set_format('np')
    encode_test_dataset.set_format('np')
    
    print(encode_train_dataset)
    print(encode_test_dataset)
    
    return encode_test_dataset, encode_train_dataset

def top_level_task(encode_test_dataset, encode_train_dataset):
    ffconfig = FFConfig()
    ffmodel = FFModel(ffconfig)
    
    

    # Load train data as numpy arrays
    print("Loading data...")    
    input_ids = encode_train_dataset['input_ids']
    attention_masks = encode_train_dataset['attention_mask']
    labels = encode_train_dataset['label'].reshape(-1, 1)
    
    
    print(input_ids.shape)
    print(attention_masks.shape)
    print(labels.shape)

    batch_size = ffconfig.batch_size
    
    input_ids_shape = (batch_size, input_ids.shape[1])
    
    attention_mask_shape = (batch_size, attention_masks.shape[1])
    labels_shape = (batch_size, labels.shape[1])
    input_tensors = [
        ffmodel.create_tensor(input_ids_shape, DataType.DT_INT64),          # input_ids
        ffmodel.create_tensor(attention_mask_shape, DataType.DT_INT64),     # attention_mask
        ffmodel.create_tensor(labels_shape, DataType.DT_INT64),  # labels
    ]
    
    input_names = ["input_ids", "attention_mask", "labels"]

    print("Tracing the model...")
    hf_model = PyTorchModel(
        model, is_hf_model=True, input_names=input_names,
        batch_size=batch_size
    )
    output_tensors = hf_model.torch_to_ff(ffmodel, input_tensors, verbose=True)
    ffoptimizer = SGDOptimizer(ffmodel, lr=0.01)

    print("Compiling the model...")
    ffmodel.compile(
        optimizer=ffoptimizer,
        loss_type=LossType.LOSS_SPARSE_CATEGORICAL_CROSSENTROPY,
        metrics=[
            MetricsType.METRICS_ACCURACY,
            MetricsType.METRICS_SPARSE_CATEGORICAL_CROSSENTROPY,
        ],
    )

    print("Creating ff data loaders...")
    input_ids_dl = ffmodel.create_data_loader(input_tensors[0], ids)
    attention_mask_dl = ffmodel.create_data_loader(input_tensors[1], mask)
    labels_dl = ffmodel.create_data_loader(input_tensors[2], y_ids)
    # NOTE: We cast down the label tensor data to 32-bit to accommodate the
    # label tensor's required dtype
    labels_dl = ffmodel.create_data_loader(
        ffmodel.label_tensor, labels_dl.astype("int32")
    )

    print("Initializing model layers...")
    ffmodel.init_layers()

    print("Training...")
    epochs = ffconfig.epochs
    ffmodel.fit(
        x=[input_ids_dl, attention_mask_dl, labels_dl],
        y=labels_dl, batch_size=batch_size, epochs=epochs,
    )
    
if __name__ == "__main__":
    print("------")
    encode_test_dataset, encode_train_dataset = prepare_numpy_dataset()
    config.pad_token_id = tokenizer.pad_token_id
    model.resize_token_embeddings(len(tokenizer)) 
    model.config.problem_type = 'single_label_classification'
    #start train&validation
    top_level_task(encode_test_dataset, encode_train_dataset)