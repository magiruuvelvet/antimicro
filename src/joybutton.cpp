#include <QDebug>
#include <QStringList>
#include <cmath>

#include "joybutton.h"
#include "event.h"

const QString JoyButton::xmlName = "button";

JoyButton::JoyButton(QObject *parent) :
    QObject(parent)
{
    slotiter = 0;
    connect(&pauseTimer, SIGNAL(timeout()), this, SLOT(pauseEvent()));
    connect(&pauseWaitTimer, SIGNAL(timeout()), this, SLOT(pauseWaitEvent()));
    connect(&holdTimer, SIGNAL(timeout()), this, SLOT(holdEvent()));
    connect(&createDeskTimer, SIGNAL(timeout()), this, SLOT(waitForDeskEvent()));
    connect(&releaseDeskTimer, SIGNAL(timeout()), this, SLOT(waitForReleaseDeskEvent()));

    this->reset();
    index = 0;
    originset = 0;

    quitEvent = true;
}

JoyButton::JoyButton(int index, int originset, QObject *parent) :
    QObject(parent)
{
    slotiter = 0;
    connect(&pauseTimer, SIGNAL(timeout()), this, SLOT(pauseEvent()));
    connect(&pauseWaitTimer, SIGNAL(timeout()), this, SLOT(pauseWaitEvent()));
    connect(&holdTimer, SIGNAL(timeout()), this, SLOT(holdEvent()));
    connect(&createDeskTimer, SIGNAL(timeout()), this, SLOT(waitForDeskEvent()));
    connect(&releaseDeskTimer, SIGNAL(timeout()), this, SLOT(waitForReleaseDeskEvent()));

    this->reset();
    this->index = index;
    this->originset = originset;

    quitEvent = true;
}

JoyButton::~JoyButton()
{
    reset();
}

void JoyButton::joyEvent(bool pressed, bool ignoresets)
{
    buttonMutex.lock();

    if (toggle && pressed && (pressed != isDown))
    {
        this->ignoresets = ignoresets;
        isButtonPressed = !isButtonPressed;
        isDown = true;
        emit clicked(index);

        ignoreSetQueue.enqueue(ignoresets);
        isButtonPressedQueue.enqueue(isButtonPressed);

        if (isButtonPressed)
        {
            buttonHold.restart();
            createDeskTimer.start(0);
            //QTimer::singleShot(0, this, SLOT(waitForDeskEvent()));
        }
        else
        {
            releaseDeskTimer.start(0);
            //QTimer::singleShot(0, this, SLOT(waitForReleaseDeskEvent()));
        }
    }
    else if (toggle && !pressed && isDown)
    {
        isDown = false;
        bool releasedCalled = distanceTempEvent();
        if (releasedCalled)
        {
            //createDeskEvent();
            buttonHold.restart();
            createDeskTimer.start(0);
            //QTimer::singleShot(0, this, SLOT(waitForDeskEvent()));
        }

        emit released (index);
    }

    else if (!toggle && (pressed != isButtonPressed))
    {
        if (pressed)
        {
            emit clicked(index);
        }
        else
        {
            emit released(index);
        }

        this->ignoresets = ignoresets;
        isButtonPressed = pressed;

        ignoreSetQueue.enqueue(ignoresets);
        isButtonPressedQueue.enqueue(isButtonPressed);

        if (useTurbo && isButtonPressed)
        {
            buttonHold.restart();
            connect(&turboTimer, SIGNAL(timeout()), this, SLOT(turboEvent()));
            turboTimer.start();
        }
        else if (useTurbo && !isButtonPressed)
        {
            turboTimer.stop();
            disconnect(&turboTimer, SIGNAL(timeout()), 0, 0);
            if (isKeyPressed)
            {
                QTimer::singleShot(0, this, SLOT(turboEvent()));
            }
        }
        else if (isButtonPressed)
        {
            buttonHold.restart();
            createDeskTimer.start(0);
            //QTimer::singleShot(0, this, SLOT(waitForDeskEvent()));
        }
        else
        {
            releaseDeskTimer.start(0);
            //QTimer::singleShot(0, this, SLOT(waitForReleaseDeskEvent()));
        }
    }
    else if (!useTurbo && isButtonPressed)
    //if (pressed)
    {
        bool releasedCalled = distanceTempEvent();
        if (releasedCalled)
        {
            //createDeskEvent();
            buttonHold.restart();
            createDeskTimer.start(0);
            //QTimer::singleShot(0, this, SLOT(waitForDeskEvent()));
        }
    }

    buttonMutex.unlock();
}

int JoyButton::getJoyNumber()
{
    return index;
}

int JoyButton::getRealJoyNumber()
{
    return index + 1;
}

void JoyButton::setJoyNumber(int index)
{
    this->index = index;
}

void JoyButton::setToggle(bool toggle)
{
    if (toggle != this->toggle)
    {
        this->toggle = toggle;
        emit toggleChanged(toggle);
    }
}

void JoyButton::setTurboInterval(int interval)
{
    if (interval > 0 && interval != this->turboInterval)
    {
        this->turboInterval = interval;
        emit turboIntervalChanged(interval);
    }
}

void JoyButton::reset()
{
    turboTimer.stop();
    pauseTimer.stop();
    pauseWaitTimer.stop();
    createDeskTimer.stop();
    releaseDeskTimer.stop();

    if (slotiter)
    {
        delete slotiter;
        slotiter = 0;
    }

    releaseDeskEvent(true);
    clearAssignedSlots();

    isButtonPressedQueue.clear();
    ignoreSetQueue.clear();
    mouseEventQueue.clear();

    currentCycle = 0;
    previousCycle = 0;
    currentPause = 0;
    currentHold = 0;
    currentDistance = 0;
    currentRawValue = 0;
    currentMouseEvent = 0;

    isKeyPressed = isButtonPressed = false;
    toggle = false;
    turboInterval = 0;
    isDown = false;
    useTurbo = false;
    mouseSpeedX = 50;
    mouseSpeedY = 50;
    setSelection = -1;
    setSelectionCondition = SetChangeDisabled;
    ignoresets = false;
}

void JoyButton::reset(int index)
{
    JoyButton::reset();
    this->index = index;
}

bool JoyButton::getToggleState()
{
    return toggle;
}

int JoyButton::getTurboInterval()
{
    return turboInterval;
}

void JoyButton::turboEvent()
{
    if (!isKeyPressed)
    {
        createDeskEvent();
        isKeyPressed = true;
        turboTimer.start(100);
    }
    else
    {
        buttonMutex.lock();
        releaseDeskEvent();
        buttonMutex.unlock();

        isKeyPressed = false;
        turboTimer.start(turboInterval - 100);
    }
}

bool JoyButton::distanceTempEvent()
{
    bool released = false;

    if (slotiter)
    {
        bool distanceFound = containsDistanceSlots();

        if (distanceFound)
        {
            double currentDistance = getDistanceFromDeadZone();
            double tempDistance = 0.0;
            JoyButtonSlot *previousDistanceSlot = 0;
            JoyButtonSlot *finalDistanceSlot = 0;
            QListIterator<JoyButtonSlot*> iter(assignments);
            if (previousCycle)
            {
                iter.findNext(previousCycle);
            }

            while (iter.hasNext())
            {
                JoyButtonSlot *slot = iter.next();
                int tempcode = slot->getSlotCode();
                if (slot->getSlotMode() == JoyButtonSlot::JoyDistance)
                {
                    tempDistance += tempcode / 100.0;

                    if (currentDistance < tempDistance)
                    {
                        /*if (this->currentDistance && previousDistanceSlot && this->currentDistance != previousDistanceSlot)
                        {
                            // Release stuff
                            releaseDeskEvent(true);

                            slotiter->toFront();
                            slotiter->findNext(previousDistanceSlot);

                            this->currentDistance = 0;
                            released = true;
                        }*/

                        finalDistanceSlot = slot;
                        iter.toBack();
                    }
                    else
                    {
                        previousDistanceSlot = slot;
                    }
                }
                // Reset tempDistance
                /*else if (slot->getSlotMode() == JoyButtonSlot::JoyCycle)
                {
                    tempDistance = 0.0;
                    //previousDistanceSlot = 0;
                }*/
            }

            // Beginning
            if (!previousDistanceSlot)
            {
                if (this->currentDistance)
                {
                    pauseTimer.stop();
                    pauseWaitTimer.stop();
                    holdTimer.stop();

                    // Release stuff
                    releaseDeskEvent(true);
                    currentPause = currentHold = 0;

                    slotiter->toFront();
                    if (previousCycle)
                    {
                        slotiter->findNext(previousCycle);
                    }

                    this->currentDistance = 0;
                    released = true;
                }
            }
            // End
            else if (previousDistanceSlot && !finalDistanceSlot)
            {
                if (this->currentDistance != previousDistanceSlot)
                {
                    pauseTimer.stop();
                    pauseWaitTimer.stop();
                    holdTimer.stop();

                    // Release stuff
                    releaseDeskEvent(true);
                    currentPause = currentHold = 0;

                    slotiter->toFront();
                    if (previousCycle)
                    {
                        slotiter->findNext(previousCycle);
                    }

                    slotiter->findNext(previousDistanceSlot);

                    this->currentDistance = previousDistanceSlot;
                    released = true;
                }
            }
            // Middle
            else if (previousDistanceSlot && finalDistanceSlot)
            {
                if (this->currentDistance != previousDistanceSlot)
                {
                    pauseTimer.stop();
                    pauseWaitTimer.stop();
                    holdTimer.stop();

                    // Release stuff
                    releaseDeskEvent(true);
                    currentPause = currentHold = 0;

                    slotiter->toFront();
                    if (previousCycle)
                    {
                        slotiter->findNext(previousCycle);
                    }

                    slotiter->findNext(previousDistanceSlot);

                    this->currentDistance = previousDistanceSlot;
                    released = true;
                }
            }
        }
    }

    return released;
}

void JoyButton::createDeskEvent()
{
    buttonMutex.lock();

    quitEvent = false;

    if (!slotiter)
    {
        slotiter = new QListIterator<JoyButtonSlot*> (assignments);
        distanceTempEvent();
        quitEvent = false;
    }
    else if (!slotiter->hasPrevious())
    {
        distanceTempEvent();
        quitEvent = false;
    }
    else if (currentCycle)
    {
        quitEvent = false;
        currentCycle = 0;
    }

    bool exit = false;

    while (slotiter->hasNext() && !exit)
    {
        JoyButtonSlot *slot = slotiter->next();
        int tempcode = slot->getSlotCode();
        JoyButtonSlot::JoySlotInputAction mode = slot->getSlotMode();

        if (mode == JoyButtonSlot::JoyKeyboard || mode == JoyButtonSlot::JoyMouseButton)
        {
            sendevent(tempcode, true, mode);
            activeSlots.append(slot);
        }
        else if (mode == JoyButtonSlot::JoyMouseMovement)
        {
            slot->getMouseInterval()->restart();
            currentMouseEvent = slot;
            activeSlots.append(slot);
            mouseEvent();
            currentMouseEvent = 0;
        }
        else if (mode == JoyButtonSlot::JoyPause)
        {
            currentPause = slot;
            pauseHold.restart();
            pauseTimer.start(0);
            exit = true;
        }
        else if (mode == JoyButtonSlot::JoyHold)
        {
            currentHold = slot;
            holdTimer.start(0);
            exit = true;
        }
        else if (mode == JoyButtonSlot::JoyCycle)
        {
            currentCycle = slot;
            exit = true;
        }
        else if (mode == JoyButtonSlot::JoyDistance)
        {
            exit = true;
        }
    }

    if (currentCycle)
    {
        quitEvent = true;
    }
    else if (!currentPause && !currentHold)
    {
        quitEvent = true;
    }

    if (quitEvent)
    {
        if (!isButtonPressedQueue.isEmpty())
        {
            bool tempButtonPressed = isButtonPressedQueue.last();
            if (tempButtonPressed && setSelectionCondition == SetChangeWhileHeld)
            {
                QTimer::singleShot(0, this, SLOT(checkForSetChange()));
            }
        }
    }

    buttonMutex.unlock();
}

void JoyButton::mouseEvent()
{
    JoyButtonSlot *buttonslot = 0;
    if (currentMouseEvent)
    {
        buttonslot = currentMouseEvent;
    }
    else if (!mouseEventQueue.isEmpty())
    {
        buttonslot = mouseEventQueue.dequeue();
    }

    if (buttonslot)
    {
        QTime* mouseInterval = buttonslot->getMouseInterval();

        int mousemode = buttonslot->getSlotCode();
        int mousespeed = 0;
        int timeElapsed = mouseInterval->elapsed();

        if (mousemode == JoyButtonSlot::MouseRight)
        {
            mousespeed = mouseSpeedX;
        }
        else if (mousemode == JoyButtonSlot::MouseLeft)
        {
            mousespeed = mouseSpeedX;
        }
        else if (mousemode == JoyButtonSlot::MouseDown)
        {
            mousespeed = mouseSpeedY;
        }
        else if (mousemode == JoyButtonSlot::MouseUp)
        {
            mousespeed = mouseSpeedY;
        }

        bool isActive = activeSlots.contains(buttonslot);
        if (isActive && timeElapsed >= 5)
        {
            int mouse1 = 0;
            int mouse2 = 0;
            double sumDist = buttonslot->getMouseDistance();

            if (mousemode == JoyButtonSlot::MouseRight)
            {
                sumDist += (mousespeed * JoyButtonSlot::JOYSPEED * timeElapsed) / 1000.0;
                int distance = (int)floor(sumDist + 0.5);
                mouse1 = distance;
            }
            else if (mousemode == JoyButtonSlot::MouseLeft)
            {
                sumDist += (mousespeed * JoyButtonSlot::JOYSPEED * timeElapsed) / 1000.0;
                int distance = (int)floor(sumDist + 0.5);
                mouse1 = -distance;
            }
            else if (mousemode == JoyButtonSlot::MouseDown)
            {
                sumDist += (mousespeed * JoyButtonSlot::JOYSPEED * timeElapsed) / 1000.0;
                int distance = (int)floor(sumDist + 0.5);
                mouse2 = distance;
            }
            else if (mousemode == JoyButtonSlot::MouseUp)
            {
                sumDist += (mousespeed * JoyButtonSlot::JOYSPEED * timeElapsed) / 1000.0;
                int distance = (int)floor(sumDist + 0.5);
                mouse2 = -distance;
            }

            if (sumDist < 1.0)
            {
                buttonslot->setDistance(sumDist);
            }
            else if (sumDist >= 1.0)
            {
                sendevent(mouse1, mouse2);
                sumDist = 0.0;

                buttonslot->setDistance(sumDist);
            }

            mouseInterval->restart();
        }

        if (isActive)
        {
            mouseEventQueue.enqueue(buttonslot);
            QTimer::singleShot(5, this, SLOT(mouseEvent()));
        }
        else
        {
            buttonslot->setDistance(0.0);
            mouseInterval->restart();
        }
    }


}

void JoyButton::setUseTurbo(bool useTurbo)
{
    bool initialState = this->useTurbo;

    if (useTurbo != this->useTurbo)
    {
        if (useTurbo && this->containsSequence())
        {
            this->useTurbo = false;
        }
        else
        {
            this->useTurbo = useTurbo;
        }

        if (initialState != this->useTurbo)
        {
            emit turboChanged(this->useTurbo);
        }
    }
}

bool JoyButton::isUsingTurbo()
{
    return useTurbo;
}

QString JoyButton::getXmlName()
{
    return this->xmlName;
}

void JoyButton::readConfig(QXmlStreamReader *xml)
{
    if (xml->isStartElement() && xml->name() == getXmlName())
    {
        reset();

        xml->readNextStartElement();
        while (!xml->atEnd() && (!xml->isEndElement() && xml->name() != getXmlName()))
        {
            if (xml->name() == "toggle" && xml->isStartElement())
            {
                QString temptext = xml->readElementText();
                if (temptext == "true")
                {
                    this->setToggle(true);
                }
            }
            else if (xml->name() == "turbointerval" && xml->isStartElement())
            {
                QString temptext = xml->readElementText();
                int tempchoice = temptext.toInt();
                this->setTurboInterval(tempchoice);
            }
            else if (xml->name() == "useturbo" && xml->isStartElement())
            {
                QString temptext = xml->readElementText();
                if (temptext == "true")
                {
                    this->setUseTurbo(true);
                }
            }
            else if (xml->name() == "mousespeedx" && xml->isStartElement())
            {
                QString temptext = xml->readElementText();
                int tempchoice = temptext.toInt();
                this->setMouseSpeedX(tempchoice);
            }
            else if (xml->name() == "mousespeedy" && xml->isStartElement())
            {
                QString temptext = xml->readElementText();
                int tempchoice = temptext.toInt();
                this->setMouseSpeedY(tempchoice);
            }
            else if (xml->name() == "slots" && xml->isStartElement())
            {
                xml->readNextStartElement();
                while (!xml->atEnd() && (!xml->isEndElement() && xml->name() != "slots"))
                {
                    if (xml->name() == "slot" && xml->isStartElement())
                    {
                        JoyButtonSlot *buttonslot = new JoyButtonSlot();
                        buttonslot->readConfig(xml);
                        setAssignedSlot(buttonslot->getSlotCode(), buttonslot->getSlotMode());
                        //this->assignments.append(buttonslot);
                    }
                    xml->readNextStartElement();
                }
            }
            else if (xml->name() == "setselect" && xml->isStartElement())
            {
                QString temptext = xml->readElementText();
                int tempchoice = temptext.toInt();
                if (tempchoice >= 1 && tempchoice <= 8)
                {
                    this->setChangeSetSelection(tempchoice - 1);
                }
            }
            else if (xml->name() == "setselectcondition" && xml->isStartElement())
            {
                QString temptext = xml->readElementText();
                SetChangeCondition tempcondition = SetChangeDisabled;
                if (temptext == "one-way")
                {
                    tempcondition = SetChangeOneWay;
                }
                else if (temptext == "two-way")
                {
                    tempcondition = SetChangeTwoWay;
                }
                else if (temptext == "while-held")
                {
                    tempcondition = SetChangeWhileHeld;
                }

                if (tempcondition != SetChangeDisabled)
                {
                    this->setChangeSetCondition(tempcondition, true);
                }
            }
            else
            {
                xml->skipCurrentElement();
            }

            xml->readNextStartElement();
        }
    }

}

void JoyButton::writeConfig(QXmlStreamWriter *xml)
{
    xml->writeStartElement(getXmlName());
    xml->writeAttribute("index", QString::number(getRealJoyNumber()));

    xml->writeTextElement("toggle", toggle ? "true" : "false");
    xml->writeTextElement("turbointerval", QString::number(turboInterval));
    xml->writeTextElement("useturbo", useTurbo ? "true" : "false");
    xml->writeTextElement("mousespeedx", QString::number(mouseSpeedX));
    xml->writeTextElement("mousespeedy", QString::number(mouseSpeedY));
    if (setSelectionCondition != SetChangeDisabled)
    {
        xml->writeTextElement("setselect", QString::number(setSelection+1));

        QString temptext;
        if (setSelectionCondition == SetChangeOneWay)
        {
            temptext = "one-way";
        }
        else if (setSelectionCondition == SetChangeTwoWay)
        {
            temptext = "two-way";
        }
        else if (setSelectionCondition == SetChangeWhileHeld)
        {
            temptext = "while-held";
        }
        xml->writeTextElement("setselectcondition", temptext);
    }

    xml->writeStartElement("slots");
    QListIterator<JoyButtonSlot*> iter(assignments);
    while (iter.hasNext())
    {
        JoyButtonSlot *buttonslot = iter.next();
        buttonslot->writeConfig(xml);
    }
    xml->writeEndElement();

    xml->writeEndElement();
}

QString JoyButton::getName()
{
    QString newlabel = getPartialName();
    newlabel = newlabel.append(": ").append(getSlotsSummary());
    return newlabel;
}

QString JoyButton::getPartialName()
{
    return QString(tr("Button").append(" ").append(QString::number(getRealJoyNumber())));
}

QString JoyButton::getSlotsSummary()
{
    QString newlabel;
    int slotCount = assignments.size();

    if (slotCount > 0)
    {
        JoyButtonSlot *slot = assignments.first();
        newlabel = newlabel.append(slot->getSlotString());

        if (slotCount > 1)
        {
            newlabel = newlabel.append(" ...");
        }
    }
    else
    {
        newlabel = newlabel.append(tr("[NO KEY]"));
    }

    return newlabel;
}

QString JoyButton::getSlotsString()
{
    QString label;

    if (assignments.size() > 0)
    {
        QListIterator<JoyButtonSlot*> iter(assignments);
        QStringList stringlist;

        while (iter.hasNext())
        {
            JoyButtonSlot *slot = iter.next();
            stringlist.append(slot->getSlotString());
        }

        label = stringlist.join(", ");
    }
    else
    {
        label = label.append(tr("[NO KEY]"));
    }

    return label;
}

void JoyButton::setCustomName(QString name)
{
    customName = name;
}

QString JoyButton::getCustomName()
{
    return customName;
}

void JoyButton::setAssignedSlot(int code, JoyButtonSlot::JoySlotInputAction mode)
{
    bool slotInserted = false;
    JoyButtonSlot *slot = new JoyButtonSlot(code, mode, this);
    if (slot->getSlotMode() == JoyButtonSlot::JoyDistance)
    {
        if (slot->getSlotCode() >= 1 && slot->getSlotCode() <= 100)
        {
            double tempDistance = getTotalSlotDistance(slot);
            if (tempDistance <= 1.0)
            {
                assignments.append(slot);
                slotInserted = true;
            }
        }
    }
    else if (slot->getSlotCode() > 0)
    {
        assignments.append(slot);
        slotInserted = true;
    }

    if (slotInserted)
    {
        if (slot->getSlotMode() == JoyButtonSlot::JoyPause ||
            slot->getSlotMode() == JoyButtonSlot::JoyHold ||
            slot->getSlotMode() == JoyButtonSlot::JoyDistance
           )
        {
            setUseTurbo(false);
        }

        emit slotsChanged();
    }
    else
    {
        if (slot)
        {
            delete slot;
            slot = 0;
        }
    }
}

void JoyButton::setAssignedSlot(int code, int index, JoyButtonSlot::JoySlotInputAction mode)
{
    bool permitSlot = true;

    JoyButtonSlot *slot = new JoyButtonSlot(code, mode, this);
    if (slot->getSlotMode() == JoyButtonSlot::JoyDistance)
    {
        if (slot->getSlotCode() >= 1 && slot->getSlotCode() <= 100)
        {
            double tempDistance = getTotalSlotDistance(slot);
            if (tempDistance > 1.0)
            {
                permitSlot = false;
            }
        }
        else
        {
            permitSlot = false;
        }
    }
    else if (slot->getSlotCode() <= 0)
    {
        permitSlot = false;
    }

    if (permitSlot)
    {
        if (index >= 0 && index < assignments.count())
        {
            // Slot already exists. Override code and place into desired slot
            assignments.insert(index, slot);
        }
        else if (index >= assignments.count())
        {
            // Append code into a new slot
            assignments.append(slot);
        }

        if (slot->getSlotMode() == JoyButtonSlot::JoyPause ||
            slot->getSlotMode() == JoyButtonSlot::JoyHold ||
            slot->getSlotMode() == JoyButtonSlot::JoyDistance
           )
        {
            setUseTurbo(false);
        }

        emit slotsChanged();
    }
    else
    {
        if (slot)
        {
            delete slot;
            slot = 0;
        }
    }
}

QList<JoyButtonSlot*>* JoyButton::getAssignedSlots()
{
    QList<JoyButtonSlot*> *newassign = new QList<JoyButtonSlot*> (assignments);
    return newassign;
}

void JoyButton::setMouseSpeedX(int speed)
{
    if (speed >= 1 && speed <= 300)
    {
        mouseSpeedX = speed;
    }
}

int JoyButton::getMouseSpeedX()
{
    return mouseSpeedX;
}

void JoyButton::setMouseSpeedY(int speed)
{
    if (speed >= 1 && speed <= 300)
    {
        mouseSpeedY = speed;
    }
}

int JoyButton::getMouseSpeedY()
{
    return mouseSpeedY;
}

void JoyButton::setChangeSetSelection(int index)
{
    if (index >= 0 && index <= 7)
    {
        setSelection = index;
    }
}

int JoyButton::getSetSelection()
{
    return setSelection;
}

void JoyButton::setChangeSetCondition(SetChangeCondition condition, bool passive)
{
    if (condition != setSelectionCondition && !passive)
    {
        if (condition == SetChangeWhileHeld || condition == SetChangeTwoWay)
        {
            // Set new condition
            emit setAssignmentChanged(index, setSelection, condition);
        }
        else if (setSelectionCondition == SetChangeWhileHeld || setSelectionCondition == SetChangeTwoWay)
        {
            // Remove old condition
            emit setAssignmentChanged(index, setSelection, SetChangeDisabled);
        }

        setSelectionCondition = condition;
    }
    else if (passive)
    {
        setSelectionCondition = condition;
    }

    if (setSelectionCondition == SetChangeDisabled)
    {
        setChangeSetSelection(-1);
    }
}

JoyButton::SetChangeCondition JoyButton::getChangeSetCondition()
{
    return setSelectionCondition;
}

bool JoyButton::getButtonState()
{
    return isButtonPressed;
}

int JoyButton::getOriginSet()
{
    return originset;
}

void JoyButton::pauseEvent()
{
    if (currentPause)
    {
        if (pauseHold.elapsed() > 100)
        {
            QListIterator<JoyButtonSlot*> iter(activeSlots);
            while (iter.hasNext())
            {
                JoyButtonSlot *slot = iter.next();
                int tempcode = slot->getSlotCode();
                JoyButtonSlot::JoySlotInputAction mode = slot->getSlotMode();
                if (mode == JoyButtonSlot::JoyKeyboard)
                {
                    sendevent(tempcode, false, mode);
                }
            }

            activeSlots.clear();
            //QTimer::singleShot(0, this, SLOT(pauseWaitEvent()));
            inpauseHold.restart();
            //disconnect(&pauseTimer, SIGNAL(timeout()), 0, 0);
            pauseTimer.stop();
            pauseWaitTimer.start(0);
        }
        else
        {
            pauseTimer.start(10);
            //QTimer::singleShot(10, this, SLOT(pauseEvent()));
        }
    }
    else
    {
        pauseTimer.stop();
        pauseWaitTimer.stop();
    }
}

void JoyButton::pauseWaitEvent()
{
    if (currentPause)
    {
        if (!isButtonPressedQueue.isEmpty() && isButtonPressedQueue.size() > 2)
        {
            if (slotiter)
            {
                slotiter->toBack();

                bool lastIgnoreSetState = ignoreSetQueue.last();
                bool lastIsButtonPressed = isButtonPressedQueue.last();
                ignoreSetQueue.clear();
                isButtonPressedQueue.clear();

                //createDeskEvent();
                ignoreSetQueue.enqueue(lastIgnoreSetState);
                isButtonPressedQueue.enqueue(lastIsButtonPressed);
                currentPause = 0;
                releaseDeskTimer.stop();
                pauseWaitTimer.stop();

                slotiter->toFront();
                quitEvent = true;
            }
        }
    }

    if (currentPause)
    {
        if (inpauseHold.elapsed() < currentPause->getSlotCode())
        {
            pauseWaitTimer.start(10);
            //QTimer::singleShot(10, this, SLOT(pauseWaitEvent()));
        }
        else
        {
            QTimer::singleShot(0, this, SLOT(createDeskEvent()));
            pauseWaitTimer.stop();
            currentPause = 0;
        }
    }
    else
    {
        pauseWaitTimer.stop();
    }
}

void JoyButton::checkForSetChange()
{
    if (!ignoreSetQueue.isEmpty() && !isButtonPressedQueue.isEmpty())
    {
        bool tempFinalState = isButtonPressedQueue.last();
        bool tempFinalIgnoreSetsState = ignoreSetQueue.last();
        bool tempButtonPressed = isButtonPressedQueue.dequeue();

        if (!tempFinalIgnoreSetsState)
        {
            if (!tempFinalState && setSelectionCondition == SetChangeOneWay && setSelection > -1)
            {
                emit setChangeActivated(setSelection);
            }
            else if (!tempFinalState && setSelectionCondition == SetChangeTwoWay && setSelection > -1)
            {
                emit setChangeActivated(setSelection);
            }
            else if ((tempButtonPressed == tempFinalState) && setSelectionCondition == SetChangeWhileHeld && setSelection > -1)
            {
                emit setChangeActivated(setSelection);
            }
        }

        ignoreSetQueue.clear();
        isButtonPressedQueue.clear();
    }
}

void JoyButton::waitForDeskEvent()
{
    if (quitEvent && !isButtonPressedQueue.isEmpty())
    {
        createDeskTimer.stop();
        createDeskEvent();
    }
    /*if (!quitEvent)
    {
        QTimer::singleShot(0, this, SLOT(waitForDeskEvent()));
    }
    else
    {
        createDeskEvent();
    }*/
}

void JoyButton::waitForReleaseDeskEvent()
{
    if (quitEvent && !isButtonPressedQueue.isEmpty())
    {
        buttonMutex.lock();
        releaseDeskTimer.stop();
        releaseDeskEvent();
        buttonMutex.unlock();
    }
    /*if (!quitEvent && !isButtonPressedQueue.isEmpty() && !isButtonPressedQueue.last())
    {
        QTimer::singleShot(0, this, SLOT(waitForReleaseDeskEvent()));
    }
    else
    {
        buttonMutex.lock();
        releaseDeskEvent();
        buttonMutex.unlock();
    }*/
}

bool JoyButton::containsSequence()
{
    bool result = false;

    QListIterator<JoyButtonSlot*> tempiter(assignments);
    while (tempiter.hasNext())
    {
        JoyButtonSlot *slot = tempiter.next();
        JoyButtonSlot::JoySlotInputAction mode = slot->getSlotMode();
        if (mode == JoyButtonSlot::JoyPause ||
            mode == JoyButtonSlot::JoyHold ||
            mode == JoyButtonSlot::JoyDistance
           )
        {
            result = true;
            tempiter.toBack();
        }
    }

    return result;
}

void JoyButton::holdEvent()
{
    if (currentHold)
    {
        bool currentlyPressed = false;
        if (!isButtonPressedQueue.isEmpty())
        {
            currentlyPressed = isButtonPressedQueue.last();
        }

        // Activate hold event
        if (currentlyPressed && buttonHold.elapsed() > currentHold->getSlotCode())
        {
            QListIterator<JoyButtonSlot*> iter(activeSlots);
            while (iter.hasNext())
            {
                JoyButtonSlot *slot = iter.next();
                int tempcode = slot->getSlotCode();
                JoyButtonSlot::JoySlotInputAction mode = slot->getSlotMode();
                if (mode == JoyButtonSlot::JoyKeyboard || mode == JoyButtonSlot::JoyMouseButton)
                {
                    sendevent(tempcode, false, mode);
                }
            }

            activeSlots.clear();
            QTimer::singleShot(0, this, SLOT(createDeskEvent()));
            currentHold = 0;
            buttonHold.restart();
        }
        // Elapsed time has not occurred
        else if (currentlyPressed)
        {
            holdTimer.start(10);
            //QTimer::singleShot(10, this, SLOT(holdEvent()));
        }
        // Pre-emptive release
        else
        {
            if (slotiter)
            {
                slotiter->toBack();
                currentHold = 0;
                createDeskEvent();
            }

            //disconnect(&holdTimer, SIGNAL(timeout()), 0, 0);
            holdTimer.stop();
        }
    }
    else
    {
        holdTimer.stop();
    }
}

void JoyButton::releaseDeskEvent(bool skipsetchange)
{
    //buttonMutex.lock();

    quitEvent = false;

    if (!activeSlots.isEmpty())
    {
        QListIterator<JoyButtonSlot*> iter(activeSlots);

        while (iter.hasNext())
        {
            JoyButtonSlot *slot = iter.next();
            int tempcode = slot->getSlotCode();
            JoyButtonSlot::JoySlotInputAction mode = slot->getSlotMode();

            if (mode == JoyButtonSlot::JoyKeyboard || mode == JoyButtonSlot::JoyMouseButton)
            {
                sendevent(tempcode, false, mode);
            }
        }

        activeSlots.clear();
    }

    if (!skipsetchange && !isButtonPressedQueue.isEmpty())
    {
        bool tempButtonPressed = isButtonPressedQueue.last();
        if (!tempButtonPressed)
        {
            QTimer::singleShot(0, this, SLOT(checkForSetChange()));
        }
    }
    else
    {
        isButtonPressedQueue.clear();
        ignoreSetQueue.clear();
    }

    if (slotiter && !slotiter->hasNext())
    {
        currentCycle = 0;
        previousCycle = 0;
        this->currentDistance = 0;
        slotiter->toFront();
    }
    else if (slotiter && slotiter->hasNext() && !currentCycle)
    {
        JoyButtonSlot *tempslot = 0;
        bool exit = false;
        JoyButtonSlot *currentSlot = slotiter->peekNext();
        while (slotiter->hasNext() && !exit)
        {
            tempslot = slotiter->next();
            if (tempslot->getSlotMode() == JoyButtonSlot::JoyCycle)
            {
                currentCycle = tempslot;
                exit = true;
            }
        }

        // Didn't find any cycle. Put
        if (!currentCycle)
        {
            slotiter->toFront();
            slotiter->findNext(currentSlot);
            slotiter->previous();
        }
    }
    else if (slotiter && slotiter->hasNext() && currentCycle)
    {
        slotiter->toFront();
        slotiter->findNext(currentCycle);
    }

    if (currentCycle)
    {
        previousCycle = currentCycle;
        this->currentDistance = 0;
        currentCycle = 0;
    }

    quitEvent = true;

    //buttonMutex.unlock();
}

void JoyButton::distanceEvent()
{
    if (currentDistance)
    {
        bool currentlyPressed = false;
        double tempDistance = 0.0;

        if (!isButtonPressedQueue.isEmpty())
        {
            currentlyPressed = isButtonPressedQueue.last();
        }

        // Activate distance event
        //if (currentlyPressed && currentRawValue > 20000)

        tempDistance = getTotalSlotDistance(currentDistance);
            /*QListIterator<JoyButtonSlot*> iter(assignments);
            while (iter.hasNext())
            {
                JoyButtonSlot *slot = iter.next();
                int tempcode = slot->getSlotCode();
                JoyButtonSlot::JoySlotInputAction mode = slot->getSlotMode();
                if (mode == JoyButtonSlot::JoyDistance)
                {
                    tempDistance += tempcode / 100.0;
                    if (slot == currentDistance)
                    {
                        // Current slot found. Go to end of iterator
                        // so loop will exit
                        iter.toBack();
                    }
                }
            }*/
        //}

        if (currentlyPressed && getDistanceFromDeadZone() >= tempDistance)
        {
            QListIterator<JoyButtonSlot*> iter(activeSlots);
            while (iter.hasNext())
            {
                JoyButtonSlot *slot = iter.next();
                int tempcode = slot->getSlotCode();
                JoyButtonSlot::JoySlotInputAction mode = slot->getSlotMode();
                if (mode == JoyButtonSlot::JoyKeyboard || mode == JoyButtonSlot::JoyMouseButton)
                {
                    sendevent(tempcode, false, mode);
                }
            }

            activeSlots.clear();
            QTimer::singleShot(0, this, SLOT(createDeskEvent()));
            currentDistance = 0;
            //buttonHold.restart();
        }
        // Elapsed time has not occurred
        else if (currentlyPressed)
        {
            QTimer::singleShot(10, this, SLOT(distanceEvent()));
        }
        // Pre-emptive release
        else
        {
            if (slotiter)
            {
                slotiter->toBack();
                currentDistance = 0;
                createDeskEvent();
            }
        }
    }
}

double JoyButton::getDistanceFromDeadZone()
{
    double distance = 0.0;
    if (isButtonPressed)
    {
        distance = 1.0;
    }

    return distance;
}

double JoyButton::getTotalSlotDistance(JoyButtonSlot *slot)
{
    double tempDistance = 0.0;

    QListIterator<JoyButtonSlot*> iter(assignments);
    while (iter.hasNext())
    {
        JoyButtonSlot *currentSlot = iter.next();
        int tempcode = currentSlot->getSlotCode();
        JoyButtonSlot::JoySlotInputAction mode = currentSlot->getSlotMode();
        if (mode == JoyButtonSlot::JoyDistance)
        {
            tempDistance += tempcode / 100.0;
            if (slot == currentSlot)
            {
                // Current slot found. Go to end of iterator
                // so loop will exit
                iter.toBack();
            }
        }
        // Reset tempDistance
        else if (mode == JoyButtonSlot::JoyCycle)
        {
            tempDistance = 0.0;
        }
    }

    return tempDistance;
}

bool JoyButton::containsDistanceSlots()
{
    bool result = false;
    QListIterator<JoyButtonSlot*> iter(assignments);
    while (iter.hasNext())
    {
        JoyButtonSlot *slot = iter.next();
        if (slot->getSlotMode() == JoyButtonSlot::JoyDistance)
        {
            result = true;
            iter.toBack();
        }
    }

    return result;
}

void JoyButton::clearAssignedSlots()
{
    QListIterator<JoyButtonSlot*> iter(assignments);
    while (iter.hasNext())
    {
        JoyButtonSlot *slot = iter.next();
        if (slot)
        {
            delete slot;
            slot = 0;
        }
    }

    assignments.clear();
    emit slotsChanged();
}

void JoyButton::removeAssignedSlot(int index)
{    
    if (index >= 0 && index < assignments.size())
    {
        JoyButtonSlot *slot = assignments.takeAt(index);
        if (slot)
        {
            delete slot;
            slot = 0;
        }

        emit slotsChanged();
    }
}

void JoyButton::clearSlotsEventReset()
{
    turboTimer.stop();
    pauseTimer.stop();
    pauseWaitTimer.stop();
    createDeskTimer.stop();
    releaseDeskTimer.stop();

    if (slotiter)
    {
        delete slotiter;
        slotiter = 0;
    }

    releaseDeskEvent(true);
    clearAssignedSlots();

    isButtonPressedQueue.clear();
    ignoreSetQueue.clear();
    mouseEventQueue.clear();

    currentCycle = 0;
    previousCycle = 0;
    currentPause = 0;
    currentHold = 0;
    currentDistance = 0;
    currentRawValue = 0;
    currentMouseEvent = 0;

    isKeyPressed = isButtonPressed = false;
}