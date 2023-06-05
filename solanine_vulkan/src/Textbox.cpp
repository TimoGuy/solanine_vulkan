#include "Textbox.h"

#include "TextMesh.h"
#include "InputManager.h"


namespace textbox
{
    textmesh::TextMesh* myText = nullptr;
    std::vector<TextboxMessage> messageQueue;

    size_t currentTextIndex = 0;

    bool answeringQuery = false;
    std::vector<textmesh::TextMesh*> querySelectionTexts;
    uint32_t answeringQuerySelection = 0;
    uint32_t numQuerySelections;


    void update(const float_t& unscaledDeltaTime)
    {
        if (myText == nullptr)
            return;

        if (input::onKeyJumpPress)
        {
            // Make selection for query
            if (answeringQuery)
            {
                messageQueue.front().endingQuery.querySelectedCallback(answeringQuerySelection);

                // Answering query cleanup
                answeringQuery = false;
                answeringQuerySelection = 0;
                for (textmesh::TextMesh* tm : querySelectionTexts)
                    textmesh::destroyAndUnregisterTextMesh(tm);
                querySelectionTexts.clear();
            }

            // Advance the textbox
            currentTextIndex++;

            // If reached end of textbox text, delete front message and reset textbox index
            if (currentTextIndex >= messageQueue.front().texts.size())
            {
                messageQueue.erase(messageQueue.begin());
                currentTextIndex = 0;
            }

            // If there is a query, then set up to answer the query
            if (currentTextIndex == messageQueue.front().texts.size() - 1 &&
                messageQueue.front().useEndingQuery)
            {
                answeringQuery = true;
                answeringQuerySelection = 0;
                numQuerySelections = (uint32_t)messageQueue.front().endingQuery.queryOptions.size();
                for (uint32_t i = 0; i < numQuerySelections; i++)
                {
                    querySelectionTexts.push_back(
                        textmesh::createAndRegisterTextMesh("defaultFont", messageQueue.front().endingQuery.queryOptions[(size_t)i])
                    );
                    querySelectionTexts.back()->isPositionScreenspace = true;
                    glm_vec3_copy(vec3{ 0.75f, 0.75f, 0.0f }, querySelectionTexts.back()->renderPosition);
                }
            }

            // Destroy textbox mesh if no more textbox messages
            if (messageQueue.empty())
            {
                textmesh::destroyAndUnregisterTextMesh(myText);
                myText = nullptr;
            }
            else
            {
                textmesh::regenerateTextMeshMesh(myText, messageQueue.front().texts[currentTextIndex]);
            }
        }

        // Cycle thru query selections
        static bool prevKeyUpPressed = false;
        if (input::keyUpPressed && !prevKeyUpPressed)
            answeringQuerySelection = (answeringQuerySelection + numQuerySelections - 1) % numQuerySelections;
        prevKeyUpPressed = input::keyUpPressed;

        static bool prevKeyDownPressed = false;
        if (input::keyDownPressed && !prevKeyDownPressed)
            answeringQuerySelection = (answeringQuerySelection + 1) % numQuerySelections;
        prevKeyDownPressed = input::keyDownPressed;
    }
    
    bool isProcessingMessage()
    {
        return (myText != nullptr);
    }

    void sendTextboxMessage(TextboxMessage message)
    {
        if (myText == nullptr)
        {
            currentTextIndex = 0;
            myText = textmesh::createAndRegisterTextMesh("defaultFont", message.texts[currentTextIndex]);
            myText->isPositionScreenspace = true;
            glm_vec3_copy(vec3{ 0.5f, 0.75f, 0.0f }, myText->renderPosition);
        }
        messageQueue.push_back(message);
    }
}