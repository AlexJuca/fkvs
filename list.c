#include "list.h"
#include <stdio.h>
#include <stdlib.h>

list_t *listCreate()
{
    list_t *list;

    if ((list = malloc(sizeof(list_t))) == NULL)
        return NULL;

    list->len = 0;
    list->head = NULL;
    list->tail = NULL;
    list->free = NULL;

    return list;
}

void listEmpty(list_t *list)
{
    list_node_t *current = list->head;

    while (current != NULL) {
        list_node_t *next = current->next;
        if (list->free)
            list->free(current->val);
        free(current);
        current = next;
    }

    list->tail = NULL;
    list->head = NULL;
    list->len = 0;
}

list_t *listAddNodeToHead(list_t *list, void *value)
{
    list_node_t *node;

    if ((node = malloc(sizeof(list_node_t))) == NULL)
        return NULL;

    node->val = value;
    listLinkNodeToHead(list, node);
    return list;
}

list_t *listAddNodeToTail(list_t *list, void *value)
{
    list_node_t *node;

    if ((node = malloc(sizeof(list_node_t))) == NULL)
        return NULL;

    node->val = value;
    node->next = NULL;
    node->prev = list->tail;

    if (list->tail != NULL) {
        list->tail->next = node;
    }
    list->tail = node;

    if (list->head == NULL) {
        list->head = node;
    }

    list->len++;
    return list;
}

list_t *listInsertNode(list_t *list, list_node_t *old_node, void *value,
                       int after)
{
    list_node_t *node;

    if ((node = malloc(sizeof(list_node_t))) == NULL)
        return NULL;

    node->val = value;

    if (after) {
        node->next = old_node->next;
        node->prev = old_node;
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        node->next = old_node;
        node->prev = old_node->prev;
        if (list->head == old_node) {
            list->head = node;
        }
    }

    if (node->prev != NULL) {
        node->prev->next = node;
    }
    if (node->next != NULL) {
        node->next->prev = node;
    }

    list->len++;
    return list;
}

void listDeleteNode(list_t *list, list_node_t *node)
{
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    if (list->free)
        list->free(node->val);
    free(node);

    list->len--;
}

list_node_t *listFindNode(list_t *list, list_node_t *start, void *value)
{
    list_node_t *node = start ? start : list->head;

    while (node) {
        if (node->val == value) {
            return node;
        }
        node = node->next;
    }

    return NULL;
}

void listLinkNodeToHead(list_t *list, list_node_t *node)
{
    if (list->len == 0) {
        list->head = node;
        list->tail = node;
        node->next = NULL;
        node->prev = NULL;
    } else {
        node->next = list->head;
        node->prev = NULL;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;
}
